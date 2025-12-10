/**
 * File: can.cpp
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo implementa funções para comunicação via protocolo CAN,
 * incluindo envio e recebimento de mensagens, bem como configuração da interface CAN.
 * As mensagens enviadas são usadas para controlar o motor e o estado do barco
 * da equipe Zênite Solar.
 */

// Standard includes
#include "can.h"
#include <map>
 
/** Construtor da classe Can.
 */
Can::Can() {
}

/** Destrutor da classe Can.
 */
Can::~Can() {
}

/** Envia uma mensagem CAN pelo socket especificado.
 * 
 * @param sock Socket CAN pelo qual a mensagem será enviada.
 * @param can_id ID da mensagem CAN.
 * @param data Ponteiro para os dados da mensagem.
 * @param dlc Tamanho dos dados (Data Length Code).
 * @return true se a mensagem foi enviada com sucesso, false em caso de erro.
 */
bool Can::send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc) {
    struct can_frame frame {};
    frame.can_id = can_id;
    frame.can_dlc = dlc;
    std::memcpy(frame.data, data, dlc);

    int nbytes = write(sock, &frame, sizeof(struct can_frame));
    if (nbytes != sizeof(struct can_frame)) {
        perror("[ERRO] envio CAN");
        return false;
    }

    std::cout << "[CAN] Mensagem enviada - ID: 0x" 
              << std::hex << can_id << std::dec << " [" << (int)dlc << " bytes]" << std::endl;
    return true;
}

/** Configura a interface CAN especificada e retorna o socket.
 * 
 * @return int Socket CAN configurado ou -1 em caso de erro.
 */
int Can::setup_can(void) {
    int sock;
    struct ifreq ifr;
    struct sockaddr_can addr;

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("[ERRO] socket");
        return -1;
    }

    std::strcpy(ifr.ifr_name, getenv("CAN_INTERFACE") ? getenv("CAN_INTERFACE") : CAN_INTERFACE);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("[ERRO] ioctl");
        close(sock);
        return -1;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ERRO] bind");
        close(sock);
        return -1;
    }

    std::cout << "[INFO] Interface CAN configurada com sucesso (" 
              << ifr.ifr_name << ")\n";
    return sock;
}

/** Recebe uma mensagem CAN do socket especificado.
 * 
 * @param sock Socket CAN do qual a mensagem será recebida.
 * @param frame Referência para a estrutura can_frame onde a mensagem será armazenada.
 * @return true se a mensagem foi recebida com sucesso, false em caso de erro.
 */
bool Can::receive_can(int sock, struct can_frame& frame) {
    int nbytes = read(sock, &frame, sizeof(struct can_frame));
    if (nbytes < 0) {
        perror("[ERRO] leitura CAN");
        return false;
    }

    std::cout << "[CAN] Recebido ID: 0x" << std::hex << frame.can_id << std::dec 
              << " | Dados:";
    for (int i = 0; i < frame.can_dlc; ++i)
        std::cout << " " << std::hex << (int)frame.data[i];
    std::cout << std::dec << std::endl;

    return true;
}

/** Fecha o socket CAN.
 * 
 * @param sock Socket CAN a ser fechado.
 */
void Can::close_can(int sock) {
    if (sock >= 0) {
        close(sock);
        std::cout << "[INFO] Socket CAN fechado com sucesso.\n";
    }
}

/** Envia um comando para o MIC via CAN.
*
*   @param duty_cycle Valor do duty cycle para o motor (0-100).
*   @param motor_on Status do motor (1 = ligado, 0 = desligado).
*   @param can_sock Socket CAN já configurado.
*/
void Can::send_command_motor(int can_sock, uint8_t duty_cycle, uint8_t motor_on) {
    uint8_t dados[CAN_MSG_MCV25_MOTOR_LENGTH];

    // Preenchimento da estrutura da mensagem:
    dados[CAN_MSG_MCV25_MOTOR_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;
    dados[CAN_MSG_MCV25_MOTOR_MOTOR_BYTE] = motor_on;
    dados[CAN_MSG_MCV25_MOTOR_D_BYTE] = duty_cycle;
    dados[CAN_MSG_MCV25_MOTOR_I_BYTE] = duty_cycle == 0 ? 0 : 100; // Indicador de velocidade

    if (send_can(can_sock, CAN_MSG_MCV25_MOTOR_ID, dados, CAN_MSG_MCV25_MOTOR_LENGTH)) {
#if LOGS_INFOS_CAN
        if (duty_cycle == 0) {
            std::cout << "[INFO] Motor " << (motor_on ? "ligado" : "desligado") << "\n";
        } else {
            std::cout << "[INFO] Velocidade ajustada para " << static_cast<int>(duty_cycle) << "%\n";
        }
#endif
    } else {
#if LOGS_ERROS_CAN
        std::cerr << "[ERRO] Falha ao enviar comando de motor\n";
#endif
    }
}

/** Envia um comando para o MIC via CAN.
*
*   @param boat_on Status do barco [ON/OFF]
*   @param can_sock Socket CAN já configurado.
*/
void Can::send_boat_state(int can_sock, bool boat_on) {
    uint8_t dados[CAN_MSG_MCV25_BOAT_STATE_LENGTH] = {0};

    // Preenchimento da estrutura da mensagem:
    dados[CAN_MSG_MCV25_BOAT_STATE_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;

    if (boat_on) {
        // Seta o bit especificado sem alterar outros bits
        dados[CAN_MSG_MCV25_BOAT_STATE_BOAT_ON_BYTE] |= 
            (1 << CAN_MSG_MCV25_BOAT_STATE_BOAT_ON_BOAT_ON_BIT);
    } else {
        // Limpa o bit especificado sem alterar outros bits
        dados[CAN_MSG_MCV25_BOAT_STATE_BOAT_ON_BYTE] &= 
            ~(1 << CAN_MSG_MCV25_BOAT_STATE_BOAT_ON_BOAT_ON_BIT);
    }

    if(send_can(can_sock, CAN_MSG_MCV25_BOAT_STATE_ID, dados, CAN_MSG_MCV25_BOAT_STATE_LENGTH)){
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Barco " << (boat_on ? "ligado" : "desligado") << "\n";
#endif  
    } else {
#if LOGS_ERROS_CAN
        std::cerr << "[ERRO] Falha ao enviar comando de status do barco.\n";
#endif
    }
}

/** Envia um comando para o MIC via CAN.
*
*   @param motor_on Status do motor [ON/OFF]
*   @param can_sock Socket CAN já configurado.
*/
void Can::send_motor_state(int can_sock, bool motor_on) {
    uint8_t dados[CAN_MSG_MCV25_MOTOR_LENGTH] = {0};

    // Preenchimento da estrutura da mensagem:
    dados[CAN_MSG_MCV25_MOTOR_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;
    dados[CAN_MSG_MCV25_MOTOR_MOTOR_BYTE] = motor_on ? 1 : 0;
    dados[CAN_MSG_MCV25_MOTOR_D_BYTE] = 0;
    dados[CAN_MSG_MCV25_MOTOR_I_BYTE] = 0;

    if(send_can(can_sock, CAN_MSG_MCV25_MOTOR_ID, dados, CAN_MSG_MCV25_MOTOR_LENGTH)){
        std::cout << "[Info] Motor " << (motor_on ? "ligado" : "desligado") << "\n";
    } else {
        std::cerr << "[ERRO] Falha ao enviar comando de status do motor.\n";
    } 
}

/** Envia comando de posição da rabeta (steering) para o MIC19 via CAN.
 *
 * @param angle Ângulo em graus (0-90).
 * @param direction 0 = esquerda (bombordo), 1 = direita (estibordo).
 * @param can_sock Socket CAN configurado.
 */
void Can::send_command_tail(int can_sock, uint8_t angle, uint8_t direction) {
    uint8_t dados[CAN_MSG_MCV25_MDE_LENGTH];

    // Validar limite de ângulo
    if (angle > 90) angle = 90;

    // Preenche mensagem
    dados[CAN_MSG_MCV25_MDE_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;
    dados[CAN_MSG_MCV25_MDE_POSITION_H_BYTE] = (direction & 0x01);  // 0 ou 1
    dados[CAN_MSG_MCV25_MDE_POSITION_L_BYTE] = angle;

    if (send_can(can_sock, CAN_MSG_MCV25_MDE_ID, dados, CAN_MSG_MCV25_MDE_LENGTH)) {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Rabeta enviada via CAN: " << (int)angle << " graus, direção: " 
                  << (direction ? "direita" : "esquerda") << "\n";
#endif
    } else {
#if LOGS_ERROS_CAN
        std::cerr << "[ERRO] Falha ao enviar comando de rabeta\n";
#endif
    }
}

/** Executa comandos de voz recebidos, enviando mensagens CAN apropriadas.
 * 
 * @param comando Comando de voz reconhecido.
 * @param can_sock Socket CAN configurado.
 * @return true se um comando válido foi executado, false caso contrário.
 */
bool Can::execute_commands(const std::string& comando, int can_sock) {

    // Mapa de velocidades (motor) com status
    static std::map<std::string, MotorCommand>* velocidade_map = nullptr;
    if (velocidade_map == nullptr) {
        velocidade_map = new std::map<std::string, MotorCommand>();
        (*velocidade_map)["desligar motor"] = {0, 0};      // duty=0, motor desligado
        (*velocidade_map)["motor off"] = {0, 0};           // duty=0, motor desligado
        (*velocidade_map)["ligar motor"] = {0, 1};         // duty=0, motor ligado
        (*velocidade_map)["motor on"] = {0, 1};            // duty=0, motor ligado
        (*velocidade_map)["mudar velocidade para zero"] = {0, 1};
        (*velocidade_map)["velocidade para zero"] = {0, 1};
        (*velocidade_map)["velocidade zero"] = {0, 1};
        (*velocidade_map)["mudar velocidade para dez"] = {10, 1};
        (*velocidade_map)["velocidade para dez"] = {10, 1};
        (*velocidade_map)["velocidade dez"] = {10, 1};
        (*velocidade_map)["mudar velocidade para vinte"] = {20, 1};
        (*velocidade_map)["velocidade para vinte"] = {20, 1};
        (*velocidade_map)["velocidade vinte"] = {20, 1};
        (*velocidade_map)["mudar velocidade para trinta"] = {30, 1};
        (*velocidade_map)["velocidade para trinta"] = {30, 1};
        (*velocidade_map)["velocidade trinta"] = {30, 1};
        (*velocidade_map)["mudar velocidade para quarenta"] = {40, 1};
        (*velocidade_map)["velocidade para quarenta"] = {40, 1};
        (*velocidade_map)["velocidade quarenta"] = {40, 1};
        (*velocidade_map)["mudar velocidade para cinquenta"] = {50, 1};
        (*velocidade_map)["velocidade para cinquenta"] = {50, 1};
        (*velocidade_map)["velocidade cinquenta"] = {50, 1};
        (*velocidade_map)["mudar velocidade para sessenta"] = {60, 1};
        (*velocidade_map)["velocidade para sessenta"] = {60, 1};
        (*velocidade_map)["velocidade sessenta"] = {60, 1};
        (*velocidade_map)["mudar velocidade para setenta"] = {70, 1};
        (*velocidade_map)["velocidade para setenta"] = {70, 1};
        (*velocidade_map)["velocidade setenta"] = {70, 1};
        (*velocidade_map)["mudar velocidade para oitenta"] = {80, 1};
        (*velocidade_map)["velocidade para oitenta"] = {80, 1};
        (*velocidade_map)["velocidade oitenta"] = {80, 1};
        (*velocidade_map)["mudar velocidade para noventa"] = {90, 1};
        (*velocidade_map)["velocidade para noventa"] = {90, 1};
        (*velocidade_map)["velocidade noventa"] = {90, 1};
        (*velocidade_map)["mudar velocidade para cem"] = {100, 1};
        (*velocidade_map)["velocidade para cem"] = {100, 1};
        (*velocidade_map)["velocidade cem"] = {100, 1};
    }

    // Mapa de posições da rabeta (steering) com ângulo e direção
    static std::map<std::string, TailCommand>* rabeta_map = nullptr;
    if (rabeta_map == nullptr) {
        rabeta_map = new std::map<std::string, TailCommand>();
        
        // Direita (estibordo) = direction 1
        (*rabeta_map)["virar a direita"] = {10, 1};
        (*rabeta_map)["direita"] = {10, 1};
        (*rabeta_map)["virar a estibordo"] = {10, 1};
        (*rabeta_map)["estibordo"] = {10, 1};
        (*rabeta_map)["virar dez graus a direita"] = {10, 1};
        (*rabeta_map)["dez graus a direita"] = {10, 1};
        (*rabeta_map)["virar dez graus a estibordo"] = {10, 1};
        (*rabeta_map)["dez graus a estibordo"] = {10, 1};
        (*rabeta_map)["virar vinte graus a direita"] = {20, 1};
        (*rabeta_map)["vinte graus a direita"] = {20, 1};
        (*rabeta_map)["virar vinte graus a estibordo"] = {20, 1};
        (*rabeta_map)["vinte graus a estibordo"] = {20, 1};
        (*rabeta_map)["virar trinta graus a direita"] = {30, 1};
        (*rabeta_map)["trinta graus a direita"] = {30, 1};
        (*rabeta_map)["virar trinta graus a estibordo"] = {30, 1};
        (*rabeta_map)["trinta graus a estibordo"] = {30, 1};
        (*rabeta_map)["virar quarenta graus a direita"] = {40, 1};
        (*rabeta_map)["quarenta graus a direita"] = {40, 1};
        (*rabeta_map)["virar quarenta graus a estibordo"] = {40, 1};
        (*rabeta_map)["quarenta graus a estibordo"] = {40, 1};
        (*rabeta_map)["virar cinquenta graus a direita"] = {50, 1};
        (*rabeta_map)["cinquenta graus a direita"] = {50, 1};
        (*rabeta_map)["virar cinquenta graus a estibordo"] = {50, 1};
        (*rabeta_map)["cinquenta graus a estibordo"] = {50, 1};
        (*rabeta_map)["virar sessenta graus a direita"] = {60, 1};
        (*rabeta_map)["sessenta graus a direita"] = {60, 1};
        (*rabeta_map)["virar sessenta graus a estibordo"] = {60, 1};
        (*rabeta_map)["sessenta graus a estibordo"] = {60, 1};
        (*rabeta_map)["virar setenta graus a direita"] = {70, 1};
        (*rabeta_map)["setenta graus a direita"] = {70, 1};
        (*rabeta_map)["virar setenta graus a estibordo"] = {70, 1};
        (*rabeta_map)["setenta graus a estibordo"] = {70, 1};
        (*rabeta_map)["virar oitenta graus a direita"] = {80, 1};
        (*rabeta_map)["oitenta graus a direita"] = {80, 1};
        (*rabeta_map)["virar oitenta graus a estibordo"] = {80, 1};
        (*rabeta_map)["oitenta graus a estibordo"] = {80, 1};
        (*rabeta_map)["virar noventa graus a direita"] = {90, 1};
        (*rabeta_map)["noventa graus a direita"] = {90, 1};
        (*rabeta_map)["virar noventa graus a estibordo"] = {90, 1};
        (*rabeta_map)["noventa graus a estibordo"] = {90, 1};
        
        // Esquerda (bombordo) = direction 0
        (*rabeta_map)["virar a esquerda"] = {10, 0};
        (*rabeta_map)["esquerda"] = {10, 0};
        (*rabeta_map)["virar a bombordo"] = {10, 0};
        (*rabeta_map)["bombordo"] = {10, 0};
        (*rabeta_map)["virar dez graus a esquerda"] = {10, 0};
        (*rabeta_map)["dez graus a esquerda"] = {10, 0};
        (*rabeta_map)["virar dez graus a bombordo"] = {10, 0};
        (*rabeta_map)["dez graus a bombordo"] = {10, 0};
        (*rabeta_map)["virar vinte graus a esquerda"] = {20, 0};
        (*rabeta_map)["vinte graus a esquerda"] = {20, 0};
        (*rabeta_map)["virar vinte graus a bombordo"] = {20, 0};
        (*rabeta_map)["vinte graus a bombordo"] = {20, 0};
        (*rabeta_map)["virar trinta graus a esquerda"] = {30, 0};
        (*rabeta_map)["trinta graus a esquerda"] = {30, 0};
        (*rabeta_map)["virar trinta graus a bombordo"] = {30, 0};
        (*rabeta_map)["trinta graus a bombordo"] = {30, 0};
        (*rabeta_map)["virar quarenta graus a esquerda"] = {40, 0};
        (*rabeta_map)["quarenta graus a esquerda"] = {40, 0};
        (*rabeta_map)["virar quarenta graus a bombordo"] = {40, 0};
        (*rabeta_map)["quarenta graus a bombordo"] = {40, 0};
        (*rabeta_map)["virar cinquenta graus a esquerda"] = {50, 0};
        (*rabeta_map)["cinquenta graus a esquerda"] = {50, 0};
        (*rabeta_map)["virar cinquenta graus a bombordo"] = {50, 0};
        (*rabeta_map)["cinquenta graus a bombordo"] = {50, 0};
        (*rabeta_map)["virar sessenta graus a esquerda"] = {60, 0};
        (*rabeta_map)["sessenta graus a esquerda"] = {60, 0};
        (*rabeta_map)["virar sessenta graus a bombordo"] = {60, 0};
        (*rabeta_map)["sessenta graus a bombordo"] = {60, 0};
        (*rabeta_map)["virar setenta graus a esquerda"] = {70, 0};
        (*rabeta_map)["setenta graus a esquerda"] = {70, 0};
        (*rabeta_map)["virar setenta graus a bombordo"] = {70, 0};
        (*rabeta_map)["setenta graus a bombordo"] = {70, 0};
        (*rabeta_map)["virar oitenta graus a esquerda"] = {80, 0};
        (*rabeta_map)["oitenta graus a esquerda"] = {80, 0};
        (*rabeta_map)["virar oitenta graus a bombordo"] = {80, 0};
        (*rabeta_map)["oitenta graus a bombordo"] = {80, 0};
        (*rabeta_map)["virar noventa graus a esquerda"] = {90, 0};
        (*rabeta_map)["noventa graus a esquerda"] = {90, 0};
        (*rabeta_map)["virar noventa graus a bombordo"] = {90, 0};
        (*rabeta_map)["noventa graus a bombordo"] = {90, 0};

        // Centralizar rabeta
        (*rabeta_map)["seguir reto"] = {0, 1};  // Centro, direção indiferente
        (*rabeta_map)["reto"] = {0, 1};         // Centro, direção indiferente
        (*rabeta_map)["centralizar"] = {0, 1};  // Centro, direção indiferente
    }

    // Procurar velocidade
    auto vel_it = velocidade_map->find(comando);
    if (vel_it != velocidade_map->end()) {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando motor.\n";
#endif
        send_command_motor(can_sock, vel_it->second.duty_cycle, vel_it->second.motor_on);
        return true;
    }

    // Procurar posição da rabeta
    auto rabeta_it = rabeta_map->find(comando);
    if (rabeta_it != rabeta_map->end()) {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando posição da rabeta - Ângulo: " << (int)rabeta_it->second.angle 
                  << " graus, Direção: " << (rabeta_it->second.direction ? "direita" : "esquerda") << "\n";
#endif
        send_command_tail(can_sock, rabeta_it->second.angle, rabeta_it->second.direction);
        return true;
    }

    // Comandos de estado do barco (tratamento especial)
    if (comando == "desligar barco" || comando == "barco off") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Desligando barco.\n";
#endif
        send_boat_state(can_sock, false);
        return true;
    }
    else if (comando == "ligar barco" || comando == "barco on") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ligando barco.\n";
#endif
        send_boat_state(can_sock, true);
        return true;
    }

    // Comando não reconhecido
    if (!comando.empty()) {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Comando não reconhecido: " << comando << "\n";
#endif
    }

    return true;
}