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
 
/**
 * Envia uma mensagem CAN pelo socket especificado.
 * 
 * @param sock Socket CAN pelo qual a mensagem será enviada.
 * @param can_id ID da mensagem CAN.
 * @param data Ponteiro para os dados da mensagem.
 * @param dlc Tamanho dos dados (Data Length Code).
 * @return true se a mensagem foi enviada com sucesso, false em caso de erro.
 */
bool send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc) {
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

/**
 * Configura a interface CAN especificada e retorna o socket.
 * 
 * @return int Socket CAN configurado ou -1 em caso de erro.
 */
int setup_can(void) {
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

/**
 * Recebe uma mensagem CAN do socket especificado.
 * 
 * @param sock Socket CAN do qual a mensagem será recebida.
 * @param frame Referência para a estrutura can_frame onde a mensagem será armazenada.
 * @return true se a mensagem foi recebida com sucesso, false em caso de erro.
 */
bool receive_can(int sock, struct can_frame& frame) {
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

/**
 * Fecha o socket CAN.
 * 
 * @param sock Socket CAN a ser fechado.
 */
void close_can(int sock) {
    if (sock >= 0) {
        close(sock);
        std::cout << "[INFO] Socket CAN fechado com sucesso.\n";
    }
}

/**
*   Envia um comando para o MIC via CAN.
*
*   @param duty_cycle Valor do duty cycle para o motor (0-100).
*   @param can_sock Socket CAN já configurado.
*/
void send_command_motor(int can_sock, uint8_t duty_cycle) {
    uint8_t dados[CAN_MSG_MCV25_MOTOR_LENGTH];

    // Preenchimento da estrutura da mensagem:
    dados[CAN_MSG_MCV25_MOTOR_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;

    // MOTOR_STATE bits: MOTOR_ON (bit 0), DMS_ON (bit 1), REVERSE (bit 2)
    dados[CAN_MSG_MCV25_MOTOR_MOTOR_BYTE] = 
        (1 << CAN_MSG_MCV25_MOTOR_MOTOR_MOTOR_ON_BIT) |
        (1 << CAN_MSG_MCV25_MOTOR_MOTOR_DMS_ON_BIT); // Não setamos REVERSE

    dados[CAN_MSG_MCV25_MOTOR_D_BYTE] = duty_cycle;  // Duty Cycle (%)
    dados[CAN_MSG_MCV25_MOTOR_I_BYTE] = 100;         // Soft start (%)

    if (send_can(can_sock, CAN_MSG_MCV25_MOTOR_ID, dados, CAN_MSG_MCV25_MOTOR_LENGTH)) {
#if LOGS_INFOS_CAN   
        std::cout << "[INFO] Velocidade ajustada para " << static_cast<int>(duty_cycle) << "%\n";
#endif
    } else {
#if LOGS_ERROS_CAN
        std::cerr << "[ERRO] Falha ao enviar comando de motor\n";
#endif
    }
}

/**
*   Envia um comando para o MIC via CAN.
*
*   @param boat_on Status do barco [ON/OFF]
*   @param can_sock Socket CAN já configurado.
*/
void send_boat_state(int can_sock, bool boat_on) {
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

// ARRUMAR/TESTAR ESTE
/**
*   Envia um comando para o MIC via CAN.
*
*   @param motor_on Status do motor [ON/OFF]
*   @param can_sock Socket CAN já configurado.
*/
// void send_motor_state(int can_sock, bool motor_on) {
//     uint8_t dados[CAN_MSG_MCV25_MOTOR_STATE_LENGTH];
//     // Preenchimento da estrutura da mensagem:
//     dados[CAN_MSG_MCV25_MOTOR_STATE_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;
//     dados[CAN_MSG_MCV25_MOTOR_STATE_BYTE] = motor_on ? 1 : 0;
//     if(send_can(can_sock, CAN_MSG_MCV25_MOTOR_STATE_ID, dados, CAN_MSG_MCV25_MOTOR_STATE_LENGTH)){
//         std::cout << "[Info] Motor " << (motor_on ? "ligado" : "desligado") << "\n";
//     } else {
//         std::cerr << "[ERRO] Falha ao enviar comando de status do motor.\n";
//     } 
// }

/**
 * Envia comando de posição da rabeta (steering) para o MIC19 via CAN.
 *
 * @param posicao_raw Valor do comando (0–789, onde 395 ≈ centro).
 * @param can_sock Socket CAN configurado.
 */
void send_command_tail(int can_sock, uint16_t posicao_raw) {
    uint8_t dados[CAN_MSG_MCV25_MDE_LENGTH];

    // Limita à faixa válida do ADC
    if (posicao_raw > 789) posicao_raw = 789;

    // Reduz 0–789 para 0–255 (8 bits)
    uint8_t pos_scaled = static_cast<uint8_t>((posicao_raw * 255) / 789);

    // Preenche mensagem
    dados[CAN_MSG_MCV25_MDE_SIGNATURE_BYTE] = CAN_SIGNATURE_MCV25;
    dados[CAN_MSG_MCV25_MDE_POSITION_L_BYTE] = pos_scaled;

    if (send_can(can_sock, CAN_MSG_MCV25_MDE_ID, dados, CAN_MSG_MCV25_MDE_LENGTH)) {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Rabeta enviada via CAN: " 
                  << posicao_raw << " (scaled=" << static_cast<int>(pos_scaled) << ")\n";
#endif
    } else {
#if LOGS_ERROS_CAN
        std::cerr << "[ERRO] Falha ao enviar comando de rabeta\n";
#endif
    }
}

/**
 * Verifica e executa comandos recebidos.
 * 
 * @param comando Comando reconhecido em formato string.
 * @param can_sock Socket CAN já configurado.
 */
bool execute_commands(const std::string& comando, int can_sock) {

    if (comando == "desligar motor" || 
            comando == "motor off") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Desligando motor.\n";
#endif
        send_command_motor(can_sock, 0);
    }
    else if (comando == "ligar motor" || 
            comando == "motor on") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ligando motor.\n";
#endif
        send_command_motor(can_sock, 5); 
    }
    else if (comando == "desligar barco" || 
                comando == "barco off"){
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Desligando barco.\n";
#endif
        send_boat_state(can_sock, false);
    }
    else if (comando == "ligar barco" || 
                comando == "barco on"){
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ligando barco.\n";
#endif
        send_boat_state(can_sock, true);
    }
    else if (comando == "mudar velocidade para dez" || 
                comando == "velocidade para dez" || 
                comando == "velocidade dez") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 10%.\n";
#endif
        send_command_motor(can_sock, 10);
    }
    else if (comando == "mudar velocidade para vinte" || 
                comando == "velocidade para vinte" || 
                comando == "velocidade vinte") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 20%.\n";
#endif
        send_command_motor(can_sock, 20);
    }
    else if (comando == "mudar velocidade para trinta" || 
                comando == "velocidade para trinta" || 
                comando == "velocidade trinta") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 30%.\n";
#endif
        send_command_motor(can_sock, 30);
    }
    else if (comando == "mudar velocidade para quarenta" || 
                comando == "velocidade para quarenta" || 
                comando == "velocidade quarenta") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 40%.\n";
#endif
        send_command_motor(can_sock, 40);
    }
    else if (comando == "mudar velocidade para cinquenta" || 
                comando == "velocidade para cinquenta" || 
                comando == "velocidade cinquenta") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 50%.\n";
#endif
        send_command_motor(can_sock, 50);
    }
    else if (comando == "mudar velocidade para sessenta" || 
                comando == "velocidade para sessenta" || 
                comando == "velocidade sessenta") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 60%.\n";
#endif
        send_command_motor(can_sock, 60);
    }
    else if (comando == "mudar velocidade para setenta" || 
                comando == "velocidade para setenta" || 
                comando == "velocidade setenta") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 70%.\n";
#endif
        send_command_motor(can_sock, 70);
    }
    else if (comando == "mudar velocidade para oitenta" || 
                comando == "velocidade para oitenta" || 
                comando == "velocidade oitenta") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 80%.\n";
#endif
        send_command_motor(can_sock, 80);
    }
    else if (comando == "mudar velocidade para noventa" || 
                comando == "velocidade para noventa" || 
                comando == "velocidade noventa") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 90%.\n";
#endif
        send_command_motor(can_sock, 90);
    }
    else if (comando == "mudar velocidade para cem" || 
                comando == "velocidade para cem" || 
                comando == "velocidade cem") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando velocidade do motor para 100%.\n";
#endif
        send_command_motor(can_sock, 100);
    }
    else if (comando == "virar a direita") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Virando a rabeta para a direita.\n";
#endif
        send_command_tail(can_sock, 650);
    }
    else if (comando == "virar a esquerda" || 
            comando == "esquerda" || 
            comando == "virar a bombordo" || 
            comando == "bombordo"){
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Virando a rabeta para a esquerda.\n";
#endif
        send_command_tail(can_sock, 150);
    }
    else if (comando == "seguir reto") {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Ajustando rabeta para posição zero.\n";
#endif
        send_command_tail(can_sock, 395);
    }
    else if (!comando.empty()) {
#if LOGS_INFOS_CAN
        std::cout << "[INFO] Comando não reconhecido: " << comando << "\n";
#endif
    }

    return true;
}