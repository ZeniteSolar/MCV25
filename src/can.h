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
#define LOGS_ERROS_CAN           0                                   // Habilita ou desabilita logs de erro e de aviso
#define LOGS_INFOS_CAN           0                                   // Habilita ou desabilita logs informativos

// Inicializa o socket CAN (retorna o descritor ou -1 se erro)
int setup_can(void);

// Envia um frame CAN genérico
bool send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc);

// Recebe um frame CAN genérico
bool receive_can(int sock, struct can_frame& frame);

// Fecha o socket CAN
void close_can(int sock);

void send_command_tail(int sock, uint16_t posicao_raw);
void send_command_motor(int sock, uint8_t duty);
void send_boat_state(int sock, bool boat_on);

// Parser para comandos transcritos ("virar direita", "motor off", etc...)
bool execute_commands(const std::string& comando, int sock);

#endif
