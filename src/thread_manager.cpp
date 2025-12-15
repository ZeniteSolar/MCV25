/**
 * File: thread_manager.cpp
 * Autor: Victor Lompa Schwider
 * 
 * Implementação do gerenciador centralizado de threads
 */

#include "thread_manager.h"
#include "audio.h"
#include "can.h"
#include "vosk.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <functional>

// Singleton initialization
ThreadManager* ThreadManager::instance_ = nullptr;
std::mutex ThreadManager::instance_mutex_;

ThreadManager& ThreadManager::getInstance() {
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (instance_ == nullptr) {
            instance_ = new ThreadManager();
        }
    }
    return *instance_;
}

ThreadManager::ThreadManager()
    : audio_handler_(nullptr),
      can_handler_(nullptr),
      vosk_handler_(nullptr),
      can_socket_(-1),
      audio_device_(nullptr),
      enable_can_(true),
      enable_audio_playback_(true),
      wakeword_timeout_ms_(5000),
      recognition_timeout_ms_(5000) {
    
    // Inicializar queues (com capacidades típicas)
    audio_queue_ = std::make_unique<ThreadSafeQueue<AudioFrame>>(100);      // ~3s @ 30fps
    wakeword_queue_ = std::make_unique<ThreadSafeQueue<WakeWordEvent>>(10);
    command_queue_ = std::make_unique<ThreadSafeQueue<VoiceCommand>>(50);
    can_queue_ = std::make_unique<ThreadSafeQueue<CANCommand>>(100);
    playback_queue_ = std::make_unique<ThreadSafeQueue<std::string>>(20);
    state_queue_ = std::make_unique<ThreadSafeQueue<SystemState>>(50);
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[ThreadManager] Inicializado (Singleton)\n";
#endif
}

ThreadManager::~ThreadManager() {
    stop();
    cleanupThreads();
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[ThreadManager] Destruído\n";
#endif
}

bool ThreadManager::initialize( Audio* audio_handler, Can* can_handler, Vosk* vosk_handler, 
                                 snd_pcm_t* audio_device, int can_socket, bool enable_can, bool enable_playback) {
    
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_) {
        std::cerr << "[ERROR] ThreadManager ja esta executando\n";
        return false;
    }
    
    // Validar handlers
    if (!audio_handler || !vosk_handler || !audio_device) {
        std::cerr << "[ERROR] Audio/Vosk handler ou audio_device nao pode ser null\n";
        return false;
    }
    
    if (enable_can && (!can_handler || can_socket < 0)) {
        std::cerr << "[ERROR] CAN handler ou socket invalido\n";
        return false;
    }
    
    // Armazenar referencias
    audio_handler_ = audio_handler;
    can_handler_ = can_handler;
    vosk_handler_ = vosk_handler;
    audio_device_ = audio_device;
    can_socket_ = can_socket;
    enable_can_ = enable_can;
    enable_audio_playback_ = enable_playback;
    
    // Inicializar estado
    current_state_.audio_state = SystemState::AUDIO_IDLE;
    current_state_.recognition_state = SystemState::REC_WAITING_WAKEWORD;
    current_state_.can_state = enable_can ? SystemState::CAN_CONNECTED : SystemState::CAN_DISCONNECTED;
    current_state_.uptime_ms = 0;
    current_state_.commands_executed = 0;
    current_state_.wakeword_detections = 0;
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[ThreadManager] Inicializado com handlers\n"
              << "  - Audio: " << (audio_handler ? "OK" : "FAIL") << "\n"
              << "  - Audio Device: " << (audio_device ? "OK" : "FAIL") << "\n"
              << "  - CAN: " << (enable_can ? (can_handler ? "OK" : "FAIL") : "DISABLED") << "\n"
              << "  - Vosk: " << (vosk_handler ? "OK" : "FAIL") << "\n"
              << "  - Playback: " << (enable_playback ? "ENABLED" : "DISABLED") << "\n";
#endif
    
    return true;
}

void ThreadManager::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_) {
        std::cerr << "[WARNING] ThreadManager já está rodando\n";
        return;
    }
    
    if (!audio_handler_ || !vosk_handler_) {
        std::cerr << "[ERROR] ThreadManager não inicializado corretamente\n";
        return;
    }
    
    running_ = true;
    paused_ = false;
    shutdown_requested_ = false;
    
    // Spawnar todas as threads
    try {
        audio_capture_thread_ = std::thread(&ThreadManager::audioCapture_run, this);
        wake_word_thread_ = std::thread(&ThreadManager::wakeWordDetection_run, this);
        vosk_recognition_thread_ = std::thread(&ThreadManager::voskRecognition_run, this);
        
        if (enable_can_) {
            can_execution_thread_ = std::thread(&ThreadManager::canExecution_run, this);
        }
        
        if (enable_audio_playback_) {
            audio_playback_thread_ = std::thread(&ThreadManager::audioPlayback_run, this);
        }
        
        state_manager_thread_ = std::thread(&ThreadManager::stateManager_run, this);
        
#if THREAD_MANAGER_DEBUG
        std::cout << "[ThreadManager] Todas as threads iniciadas\n";
#endif
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Falha ao spawnar threads: " << e.what() << "\n";
        running_ = false;
    }
}

void ThreadManager::stop() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!running_) return;
        
        running_ = false;
        shutdown_requested_ = true;
    }
    
    // Notificar todas as threads que devem parar
    frame_ready_cv_.notify_all();
    wakeword_cv_.notify_all();
    command_ready_cv_.notify_all();
    can_ready_cv_.notify_all();
    playback_cv_.notify_all();
    state_changed_cv_.notify_all();
    
    // Fechar filas para acordar threads esperando
    audio_queue_->close();
    wakeword_queue_->close();
    command_queue_->close();
    can_queue_->close();
    playback_queue_->close();
    state_queue_->close();
    
    cleanupThreads();
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[ThreadManager] Parado\n";
#endif
}

void ThreadManager::pause() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    paused_ = true;
    state_changed_cv_.notify_all();
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[ThreadManager] Pausado\n";
#endif
}

void ThreadManager::resume() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    paused_ = false;
    state_changed_cv_.notify_all();
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[ThreadManager] Retomado\n";
#endif
}

void ThreadManager::audioCapture_run() {
#if THREAD_MANAGER_DEBUG
    std::cout << "[AudioCapture] Thread iniciada\n";
#endif
    
    if (!audio_handler_ || !audio_device_) {
        std::cerr << "[AudioCapture] Audio handler ou device nao disponivel\n";
        return;
    }
    
    uint32_t frame_count = 0;
    
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        try {
            // Capturar signal_t completo (ja vem com get_data pronto)
            std::vector<int16_t> raw_buffer;
            signal_t signal = audio_handler_->get_audio(audio_device_, &raw_buffer);
            
            if (signal.total_length == 0) {
                continue;
            }
            
            // Criar frame de audio
            AudioFrame frame;
            frame.raw_samples = raw_buffer;
            frame.normalized = audio_handler_->normalize(raw_buffer);
            frame.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            frame.frame_id = frame_count;
            frame.is_complete = true;
            
            // Armazenar signal_t para wake word usar (salvar em algum lugar compartilhado)
            // Por agora vamos copiar para o frame
            // Criar um wrapper que guarde o signal
            
            // Push para fila de audio (timeout curto para nao bloquear)
            if (audio_queue_->push_timeout(frame, 50)) {
                frame_count++;
                
#if THREAD_MANAGER_DEBUG
                if (frame_count % 100 == 0) {
                    std::cout << "[AudioCapture] Frame #" << frame_count 
                              << " (" << frame.normalized.size() << " samples)\n";
                }
#endif
            }
            
            // Notificar WakeWordDetection que ha audio disponivel
            frame_ready_cv_.notify_one();
            
        } catch (const std::exception& e) {
            std::cerr << "[AudioCapture] Erro: " << e.what() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[AudioCapture] Thread finalizada (" << frame_count << " frames capturados)\n";
#endif
}

void ThreadManager::wakeWordDetection_run() {
#if THREAD_MANAGER_DEBUG
    std::cout << "[WakeWord] Thread iniciada\n";
#endif
    
    if (!audio_handler_) {
        std::cerr << "[WakeWord] Audio handler nao disponivel\n";
        return;
    }
    
    uint32_t detection_count = 0;
    
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        try {
            AudioFrame frame;
            
            if (!audio_queue_->pop_timeout(frame, 1000)) {
                continue;
            }
            
            if (frame.raw_samples.empty()) {
                continue;
            }
            
            // Reconstruir signal_t com get_data callback válido
            // Usar o Audio handler para preparar a signal com os dados já normalizados
            // Nesse caso, passamos um lambda que acessa frame.normalized
            signal_t signal;
            signal.total_length = frame.normalized.size();
            
            // Criar um callback que retorna os dados do frame normalizado
            // Capturar frame.normalized por referência... MAS NAO PODE porque frame sai de escopo!
            // Solução: Usar um lambda com captura de cópia dos dados
            std::vector<float> normalized_copy = frame.normalized;
            signal.get_data = [normalized_copy](size_t offset, size_t length, float *out_ptr) -> int {
                if (!out_ptr) return -1;
                if (offset >= normalized_copy.size()) return 0;
                
                size_t to_copy = std::min(length, normalized_copy.size() - offset);
                std::memcpy(out_ptr, normalized_copy.data() + offset, to_copy * sizeof(float));
                return to_copy;
            };
            
            bool detected = wake_word_detected(&signal);
            
            if (detected) {
                detection_count++;
                
                WakeWordEvent event;
                event.confidence = 0.95f;
                event.timestamp_ms = frame.timestamp_ms;
                event.duration_ms = 1000;
                
                wakeword_queue_->push(event);
                wakeword_cv_.notify_one();
                
#if THREAD_MANAGER_DEBUG
                std::cout << "[WakeWord] DETECTADA! (" << detection_count 
                          << ") Confidence: " << event.confidence << "\n";
#endif
                
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_state_.wakeword_detections++;
                    current_state_.recognition_state = SystemState::REC_LISTENING;
                }
                
                if (enable_audio_playback_) {
                    playback_queue_->push("zenira_wakesound.wav");
                    playback_cv_.notify_one();
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "[WakeWord] Erro: " << e.what() << "\n";
        }
    }
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[WakeWord] Thread finalizada (" << detection_count << " deteccoes)\n";
#endif
}

void ThreadManager::voskRecognition_run() {
#if THREAD_MANAGER_DEBUG
    std::cout << "[Vosk] Thread iniciada\n";
#endif
    
    if (!vosk_handler_ || !audio_handler_) {
        std::cerr << "[Vosk] Vosk ou Audio handler nao disponivel\n";
        return;
    }
    
    uint32_t command_count = 0;
    
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        try {
            WakeWordEvent wakeword;
            
            if (!wakeword_queue_->pop_timeout(wakeword, 500)) {
                continue;
            }
            
#if THREAD_MANAGER_DEBUG
            std::cout << "[Vosk] Wake-word recebido, listening window iniciado\n";
#endif
            
            VoskModel* model = vosk_handler_->load_vosk_model("vosk-models/vosk-model-small-pt-0.3");
            if (!model) {
                std::cerr << "[Vosk] Falha ao carregar modelo\n";
                continue;
            }
            
            VoskRecognizer* recognizer = vosk_handler_->create_command_recognizer(model);
            if (!recognizer) {
                std::cerr << "[Vosk] Falha ao criar reconhecedor\n";
                vosk_model_free(model);
                continue;
            }
            
            auto listening_start = std::chrono::high_resolution_clock::now();
            std::string recognized_text;
            bool command_recognized = false;
            
            while (running_ && !command_recognized) {
                AudioFrame frame;
                
                if (!audio_queue_->pop_timeout(frame, 100)) {
                    auto now = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - listening_start).count();
                    
                    if (elapsed >= recognition_timeout_ms_) {
#if THREAD_MANAGER_DEBUG
                        std::cout << "[Vosk] Timeout de 5s atingido\n";
#endif
                        break;
                    }
                    continue;
                }
                
                recognized_text = vosk_handler_->vosk_process(recognizer);
                
                if (!recognized_text.empty()) {
                    command_recognized = true;
#if THREAD_MANAGER_DEBUG
                    std::cout << "[Vosk] Comando reconhecido: " << recognized_text << "\n";
#endif
                }
                
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - listening_start).count();
                
                if (elapsed >= recognition_timeout_ms_) {
                    break;
                }
            }
            
            if (command_recognized && !recognized_text.empty()) {
                command_count++;
                
                VoiceCommand command;
                command.command_text = recognized_text;
                command.confidence = 0.85f;
                command.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                command.status = VoiceCommand::PENDING;
                
                command_queue_->push(command);
                command_ready_cv_.notify_one();
                
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_state_.recognition_state = SystemState::REC_PROCESSING;
                }
                
                if (enable_audio_playback_) {
                    playback_queue_->push("command_received.wav");
                    playback_cv_.notify_one();
                }
            } else {
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_state_.recognition_state = SystemState::REC_WAITING_WAKEWORD;
                }
                
                if (enable_audio_playback_) {
                    playback_queue_->push("no_command.wav");
                    playback_cv_.notify_one();
                }
            }
            
            vosk_recognizer_free(recognizer);
            vosk_model_free(model);
            
        } catch (const std::exception& e) {
            std::cerr << "[Vosk] Erro: " << e.what() << "\n";
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                current_state_.recognition_state = SystemState::REC_WAITING_WAKEWORD;
            }
        }
    }
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[Vosk] Thread finalizada (" << command_count << " comandos reconhecidos)\n";
#endif
}

void ThreadManager::canExecution_run() {
#if THREAD_MANAGER_DEBUG
    std::cout << "[CANExecution] Thread iniciada\n";
#endif
    
    if (!enable_can_ || !can_handler_ || can_socket_ < 0) {
        std::cerr << "[CANExecution] CAN nao habilitado ou socket invalido\n";
        return;
    }
    
    uint32_t command_sent = 0;
    
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        try {
            VoiceCommand voice_cmd;
            
            if (!command_queue_->pop_timeout(voice_cmd, 100)) {
                continue;
            }
            
            if (voice_cmd.command_text.empty()) {
                continue;
            }
            
#if THREAD_MANAGER_DEBUG
            std::cout << "[CANExecution] Processando: " << voice_cmd.command_text << "\n";
#endif
            
            bool success = can_handler_->execute_commands(voice_cmd.command_text, can_socket_);
            
            if (success) {
                command_sent++;
                
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_state_.commands_executed++;
                }
                
#if THREAD_MANAGER_DEBUG
                std::cout << "[CANExecution] Sucesso\n";
#endif
            } else {
                std::cerr << "[CANExecution] Falha\n";
            }
            
            if (enable_audio_playback_) {
                playback_queue_->push("command_executed.wav");
                playback_cv_.notify_one();
            }
            
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                current_state_.recognition_state = SystemState::REC_WAITING_WAKEWORD;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "[CANExecution] Erro: " << e.what() << "\n";
        }
    }
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[CANExecution] Thread finalizada (" << command_sent << " comandos)\n";
#endif
}

void ThreadManager::audioPlayback_run() {
#if THREAD_MANAGER_DEBUG
    std::cout << "[AudioPlayback] Thread iniciada\n";
#endif
    
    if (!audio_handler_) {
        std::cerr << "[AudioPlayback] Audio handler nao disponivel\n";
        return;
    }
    
    uint32_t playback_count = 0;
    
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        try {
            std::string filename;
            
            if (!playback_queue_->pop_timeout(filename, 500)) {
                continue;
            }
            
            if (filename.empty()) {
                continue;
            }
            
            std::string full_path = "audio_files/" + filename;
            
#if THREAD_MANAGER_DEBUG
            std::cout << "[AudioPlayback] Tocando: " << full_path << "\n";
#endif
            
            try {
                audio_handler_->run_audio(full_path);
                playback_count++;
            } catch (const std::exception& e) {
                std::cerr << "[AudioPlayback] Erro ao tocar: " << e.what() << "\n";
            }
            
        } catch (const std::exception& e) {
            std::cerr << "[AudioPlayback] Erro: " << e.what() << "\n";
        }
    }
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[AudioPlayback] Thread finalizada (" << playback_count << " arquivos)\n";
#endif
}

void ThreadManager::stateManager_run() {
#if THREAD_MANAGER_DEBUG
    std::cout << "[StateManager] Thread iniciada\n";
#endif
    
    auto last_report = std::chrono::high_resolution_clock::now();
    
    while (running_) {
        try {
            // Coletar estado a cada 1 segundo
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_report).count();
            
            if (elapsed >= 1000) {
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    
                    // Atualizar estado com informações das filas
                    current_state_.audio_buffer_fill = 
                        (audio_queue_->size() * 100) / 100;
                    current_state_.command_queue_size = command_queue_->size();
                    current_state_.uptime_ms += 1000;
                }
                
                last_report = now;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
        } catch (const std::exception& e) {
            std::cerr << "[StateManager] Erro: " << e.what() << "\n";
        }
    }
    
#if THREAD_MANAGER_DEBUG
    std::cout << "[StateManager] Thread finalizada\n";
#endif
}

void ThreadManager::cleanupThreads() {
    // Join threads (com timeout)
    std::vector<std::thread*> threads = {
        &audio_capture_thread_,
        &wake_word_thread_,
        &vosk_recognition_thread_,
        &can_execution_thread_,
        &audio_playback_thread_,
        &state_manager_thread_
    };
    
    for (auto thread : threads) {
        if (thread->joinable()) {
            thread->join();
        }
    }
}

SystemState ThreadManager::getSystemState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
}

std::vector<ThreadStats> ThreadManager::getThreadStats() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return thread_stats_;
}

void ThreadManager::printQueueStats() {
    std::cout << "\n[Queue Statistics]\n";
    std::cout << "  Audio Queue:     " << audio_queue_->size() << " / 100\n";
    std::cout << "  WakeWord Queue:  " << wakeword_queue_->size() << " / 10\n";
    std::cout << "  Command Queue:   " << command_queue_->size() << " / 50\n";
    std::cout << "  CAN Queue:       " << can_queue_->size() << " / 100\n";
    std::cout << "  Playback Queue:  " << playback_queue_->size() << " / 20\n";
    std::cout << "  State Queue:     " << state_queue_->size() << " / 50\n";
}

void ThreadManager::printThreadStats() {
    SystemState state = getSystemState();
    
    std::cout << "\n[System State]\n";
    std::cout << "  Audio State:        " << (int)state.audio_state << "\n";
    std::cout << "  Recognition State:  " << (int)state.recognition_state << "\n";
    std::cout << "  CAN State:          " << (int)state.can_state << "\n";
    std::cout << "  Uptime (ms):        " << state.uptime_ms << "\n";
    std::cout << "  Commands Executed:  " << state.commands_executed << "\n";
    std::cout << "  WakeWord Detections:" << state.wakeword_detections << "\n";
}

