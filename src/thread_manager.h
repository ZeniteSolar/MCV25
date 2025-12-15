/**
 * File: thread_manager.h
 * Autor: Victor Lompa Schwider
 * 
 * Gerenciador centralizado de threads para arquitetura multi-thread
 * do sistema de reconhecimento de voz MCV25.
 * 
 * Implementa:
 * - Singleton pattern
 * - Thread pooling
 * - Queue-based communication (inter-thread)
 * - Sincronização segura com mutexes e condition variables
 * - Detecção básica de deadlock
 */

#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <alsa/asoundlib.h>

// Forward declaration
struct signal_t;  // Definido em edge_impulse.h

// Debug defines
#define THREAD_MANAGER_DEBUG    1    // Habilita debug detalhado de threads
#define THREAD_DEADLOCK_DETECT  1    // Detector de deadlock em modo debug
#define THREAD_PERF_LOGGING     0    // Log de performance (latência)

/** @struct AudioFrame
 *  Representa um frame de áudio capturado do ALSA
 *  Fluxo: AudioCapture → WakeWord/Vosk
 */
struct AudioFrame {
    std::vector<int16_t> raw_samples;      // 44.1 kHz raw PCM data
    std::vector<float> normalized;         // Normalized 16 kHz float [-1.0, 1.0]
    uint64_t timestamp_ms;                 // Timestamp de captura (ms desde boot)
    uint32_t frame_id;                     // ID sequencial do frame
    bool is_complete;                      // Flag de completude do frame
};

/** @struct WakeWordEvent
 *  Evento de detecção de wake-word (Edge Impulse)
 *  Fluxo: WakeWord → Main Orchestrator
 */
struct WakeWordEvent {
    float confidence;                      // Confiança da detecção (0.0 - 1.0)
    uint64_t timestamp_ms;                 // Timestamp de detecção
    uint64_t duration_ms;                  // Duração da janela analisada
    enum State { DETECTED, REJECTED } state;  // Estado da detecção
};

/** @struct VoiceCommand
 *  Comando de voz reconhecido pelo Vosk
 *  Fluxo: Vosk → Main → CAN Execution
 */
struct VoiceCommand {
    std::string command_text;              // Texto do comando reconhecido
    float confidence;                      // Confiança do reconhecimento (0.0 - 1.0)
    uint64_t timestamp_ms;                 // Timestamp de reconhecimento
    enum Status { 
        PENDING,                           // Aguardando execução
        EXECUTING,                         // Em execução no CAN
        EXECUTED,                          // Executado com sucesso
        FAILED                             // Falha na execução
    } status;
};

/** @struct CANCommand
 *  Comando CAN para executar no barco (motor, rabeta, etc)
 *  Fluxo: Main Orchestrator → CAN Execution Thread
 */
struct CANCommand {
    uint32_t can_id;                       // CAN message ID
    uint8_t data[8];                       // CAN payload (até 8 bytes)
    uint8_t dlc;                           // Data Length Code
    uint64_t timestamp_ms;                 // Timestamp do comando
    enum Priority { 
        LOW = 0,
        NORMAL = 1,
        HIGH = 2 
    } priority;
    std::string description;               // Debug: descrição do comando
};

/** @struct SystemState
 *  Estado global do sistema (coletado por StateManager)
 *  Fluxo: All threads → StateManager → Logging/Telemetry
 */
struct SystemState {
    enum AudioState { 
        AUDIO_IDLE,
        AUDIO_CAPTURING,
        AUDIO_PROCESSING 
    } audio_state;
    
    enum RecognitionState { 
        REC_WAITING_WAKEWORD,
        REC_LISTENING,
        REC_PROCESSING,
        REC_IDLE
    } recognition_state;
    
    enum CANState { 
        CAN_DISCONNECTED,
        CAN_CONNECTED,
        CAN_ERROR
    } can_state;
    
    uint32_t audio_buffer_fill;            // % fill (0-100)
    uint32_t command_queue_size;           // Tamanho da fila de comandos
    float cpu_usage;                       // Estimativa de CPU usage (%)
    float memory_usage;                    // Memória usada (MB)
    std::vector<std::string> active_threads;  // Threads ativas
    uint64_t uptime_ms;                    // Uptime do sistema
    uint32_t commands_executed;            // Total de comandos executados
    uint32_t wakeword_detections;          // Total de detecções
};

/** @struct ThreadStats
 *  Estatísticas de uma thread individual
 */
struct ThreadStats {
    std::string thread_name;
    uint64_t start_time_ms;
    uint64_t uptime_ms;
    uint32_t tasks_processed;
    float avg_task_duration_ms;
    float max_task_duration_ms;
    float cpu_usage;
    bool is_alive;
};

/** @template ThreadSafeQueue<T>
 *  Fila thread-safe com suporte a timeout, capacidade limitada
 *  e notificação via condition variables
 */
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    
    const size_t max_size_;
    std::atomic<bool> closed_;
    std::atomic<uint64_t> total_pushed_{0};
    std::atomic<uint64_t> total_popped_{0};
    
public:
    explicit ThreadSafeQueue(size_t max_size = 1000) 
        : max_size_(max_size), closed_(false) {}
    
    ~ThreadSafeQueue() {
        close();
    }
    
    /** Push - bloqueia se fila está cheia */
    void push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Esperar se fila está cheia
        cv_not_full_.wait(lock, [this] { 
            return queue_.size() < max_size_ || closed_; 
        });
        
        if (closed_) return;
        
        queue_.push(value);
        total_pushed_++;
        cv_not_empty_.notify_one();
    }
    
    /** Push com timeout - retorna false se timeout */
    bool push_timeout(const T& value, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        bool notified = cv_not_full_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return queue_.size() < max_size_ || closed_; }
        );
        
        if (!notified || closed_) return false;
        
        queue_.push(value);
        total_pushed_++;
        cv_not_empty_.notify_one();
        return true;
    }
    
    /** Try push - não bloqueia */
    bool try_push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.size() >= max_size_ || closed_) {
            return false;
        }
        
        queue_.push(value);
        total_pushed_++;
        cv_not_empty_.notify_one();
        return true;
    }
    
    /** Pop - bloqueia até ter elemento */
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        cv_not_empty_.wait(lock, [this] { 
            return !queue_.empty() || closed_; 
        });
        
        if (queue_.empty()) {
            throw std::runtime_error("Queue is empty and closed");
        }
        
        T value = queue_.front();
        queue_.pop();
        total_popped_++;
        cv_not_full_.notify_one();
        return value;
    }
    
    /** Pop com timeout - retorna false se timeout */
    bool pop_timeout(T& value, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        bool notified = cv_not_empty_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return !queue_.empty() || closed_; }
        );
        
        if (queue_.empty()) {
            return false;
        }
        
        value = queue_.front();
        queue_.pop();
        total_popped_++;
        cv_not_full_.notify_one();
        return true;
    }
    
    /** Try pop - não bloqueia */
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return false;
        }
        
        value = queue_.front();
        queue_.pop();
        total_popped_++;
        cv_not_full_.notify_one();
        return true;
    }
    
    /** Getter de tamanho */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    /** Check se vazio */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    /** Check se cheio */
    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size() >= max_size_;
    }
    
    /** Clear fila */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }
    
    /** Close fila - bloqueia futuros push/pop */
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
    
    /** Check se fechada */
    bool is_closed() const {
        return closed_;
    }
    
    /** Getter de estatísticas */
    uint64_t get_total_pushed() const { return total_pushed_; }
    uint64_t get_total_popped() const { return total_popped_; }
};

class ThreadManager {
private:
    // Singleton
    static ThreadManager* instance_;
    static std::mutex instance_mutex_;
    
    // Estado global
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> shutdown_requested_{false};
    
    // Threads
    std::thread audio_capture_thread_;
    std::thread wake_word_thread_;
    std::thread vosk_recognition_thread_;
    std::thread can_execution_thread_;
    std::thread audio_playback_thread_;
    std::thread state_manager_thread_;
    
    // Inter-thread Communication Queues
    std::unique_ptr<ThreadSafeQueue<AudioFrame>> audio_queue_;
    std::unique_ptr<ThreadSafeQueue<WakeWordEvent>> wakeword_queue_;
    std::unique_ptr<ThreadSafeQueue<VoiceCommand>> command_queue_;
    std::unique_ptr<ThreadSafeQueue<CANCommand>> can_queue_;
    std::unique_ptr<ThreadSafeQueue<std::string>> playback_queue_;
    std::unique_ptr<ThreadSafeQueue<SystemState>> state_queue_;
    
    // Mutexes (Hierarquia: STATE > AUDIO > VOSK/WAKEWORD/CAN > PLAYBACK)
    std::mutex state_mutex_;      // Level 1 (HIGHEST)
    std::mutex audio_mutex_;      // Level 2
    std::mutex vosk_mutex_;       // Level 3
    std::mutex wakeword_mutex_;   // Level 3
    std::mutex can_mutex_;        // Level 4
    std::mutex playback_mutex_;   // Level 3
    
    // Condition Variables
    std::condition_variable frame_ready_cv_;
    std::condition_variable wakeword_cv_;
    std::condition_variable command_ready_cv_;
    std::condition_variable can_ready_cv_;
    std::condition_variable playback_cv_;
    std::condition_variable state_changed_cv_;
    
    // Thread state tracking
    SystemState current_state_;
    std::vector<ThreadStats> thread_stats_;
    
    // Configuração
    bool enable_can_;
    bool enable_audio_playback_;
    int wakeword_timeout_ms_;
    int recognition_timeout_ms_;
    
    // Callbacks/Pointers para componentes (injetados)
    class Audio* audio_handler_;
    class Can* can_handler_;
    class Vosk* vosk_handler_;
    int can_socket_;
    snd_pcm_t* audio_device_;  // Device ALSA compartilhado
    
    // Private Constructor (Singleton)
    ThreadManager();
    
    // Funções de execução das threads
    void audioCapture_run();
    void wakeWordDetection_run();
    void voskRecognition_run();
    void canExecution_run();
    void audioPlayback_run();
    void stateManager_run();
    
    // Helpers
    void syncThreads();
    bool checkHealth();
    void cleanupThreads();
    
public:
    // Singleton access
    static ThreadManager& getInstance();
    
    // Lifecycle
    bool initialize(
        class Audio* audio_handler,
        class Can* can_handler,
        class Vosk* vosk_handler,
        snd_pcm_t* audio_device,
        int can_socket,
        bool enable_can = true,
        bool enable_playback = true
    );
    
    void start();
    void stop();
    void pause();
    void resume();
    
    ~ThreadManager();
    
    // Queue getters (para push de eventos externos)
    ThreadSafeQueue<AudioFrame>* getAudioQueue() { return audio_queue_.get(); }
    ThreadSafeQueue<std::string>* getPlaybackQueue() { return playback_queue_.get(); }
    ThreadSafeQueue<CANCommand>* getCANQueue() { return can_queue_.get(); }
    
    // State getters
    SystemState getSystemState();
    std::vector<ThreadStats> getThreadStats();
    
    // Debug/Monitoring
    void printQueueStats();
    void printThreadStats();
};

#endif // THREAD_MANAGER_H
