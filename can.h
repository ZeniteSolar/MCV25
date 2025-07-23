#ifndef CAN_H
#define CAN_H

#include <cstdint>          
#include <cstring>          
#include <iostream>         
#include <cstdio>           
#include <unistd.h>         
#include <sys/ioctl.h>      
#include <net/if.h>          
#include <linux/can.h>       
#include <linux/can/raw.h>  
#include <sys/types.h>      
#include <sys/socket.h>  
#include <cerrno>

/*
* Função para enviar uma mensagem CAN 
*
* @param sock Socket CAN aberto
* @param can_id ID do frame CAN
* @param data Dados a serem enviados
* @param dlc Comprimento do frame (Data Length Code)
* @return true se o envio foi bem-sucedido, false caso contrário
*/
bool send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc);

/*
* Função para receber uma mensagem CAN
*
* @param sock Socket CAN aberto
* @param frame Estrutura can_frame onde os dados recebidos serão armazenados
* @return true se a leitura foi bem-sucedida, false caso contrário
*/
bool receive_can(int sock, struct can_frame& frame);

/*
* Função para configurar o socket CAN
*
* @return O socket CAN aberto ou -1 em caso de erro
*/
int setup_can(void);

/*
* Função para fechar o socket CAN
*
* @param sock Socket CAN a ser fechado
*/
void close_can(int sock);

#endif
