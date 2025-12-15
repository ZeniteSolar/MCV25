/**
 * File: can.h
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo declara funções para comunicação via protocolo CAN,
 * incluindo envio e recebimento de mensagens, bem como configuração da interface CAN.
 * As mensagens enviadas são usadas para controlar o motor e o estado do barco
 * da equipe Zênite Solar.
 */

#ifndef CAN_H
#define CAN_H

#include "can_ids.h"
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <net/if.h> 
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <cstdint>

#define CAN_INTERFACE "can0"  // Interface padrão do MCP2515

// Debug configs
#define LOGS_ERROS_CAN 0  // Habilita ou desabilita logs de erro e de aviso
#define LOGS_INFOS_CAN 1  // Habilita ou desabilita logs informativos

// Estrutura para armazenar ângulo e direção da rabeta
struct TailCommand {
    uint8_t angle;      // Ângulo em graus (0-90)
    uint8_t direction;  // 0 = esquerda (bombordo), 1 = direita (estibordo)
};

// Estrutura para armazenas duty cycle e estado do motor
struct MotorCommand {
    uint8_t duty_cycle;
    uint8_t motor_on;  // 1 = motor ligado, 0 = motor desligado
};


class Can {
public:
    Can();
    ~Can();

    // Metodos de inicializacao e finalizacao
    int setup_can(void);
    void close_can(int sock);

    // Metodos de envio e recebimento
    bool send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc);
    bool receive_can(int sock, struct can_frame& frame);

    // Metodo geral de comando
    bool execute_commands(const std::string& comando, int sock);

private:

    // Metodos de comando especificos
    void send_command_motor(int sock, uint8_t duty_cycle, uint8_t motor_on);
    void send_command_tail(int sock, uint8_t angle, uint8_t direction);
    void send_boat_state(int sock, bool boat_on);
    void send_motor_state(int can_sock, bool motor_on);

};

#endif
