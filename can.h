#ifndef CAN_H
#define CAN_H

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

// Inicializa o socket CAN (retorna o descritor ou -1 se erro)
int setup_can(void);

// Envia um frame CAN genérico
bool send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc);

// Recebe um frame CAN genérico
bool receive_can(int sock, struct can_frame& frame);

// Fecha o socket CAN
void close_can(int sock);

#endif
