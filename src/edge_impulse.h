/**
 * File: edge_impulse.h
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo declara funções relacionadas à integração com o Edge Impulse SDK
 */

#ifndef EDGE_IMPULSE_H
#define EDGE_IMPULSE_H

// Standard includes
#include <iostream>
#include <cstring>

// Edge Impulse type headers
#include "edge-impulse-sdk/classifier/ei_classifier_types.h"
#include "edge-impulse-sdk/dsp/numpy_types.h"

// Usar signal_t do namespace ei
using signal_t = ei::signal_t;

// Debug configs
#define LOGS_ERRO_EI           0                                   // Habilita ou desabilita logs de erro e de aviso
#define LOGS_INFO_EI           1                                   // Habilita ou desabilita logs informativos
#define CLASSIFIER_VERBOSE     1
#define EI_THRESHOLD_WAKE_WORD 0.7f                               // Limiar de confiança para detecção da wake-word
/**
 *  Detecta a palavra-chave "Zenira" no áudio capturado.
 * 
 *  @param signal Ponteiro para a estrutura signal_t que será preenchida.
 *  @return true se a palavra-chave foi detectada, false caso contrário ou em caso de erro.
 */
bool wake_word_detected(signal_t* signal);

#endif