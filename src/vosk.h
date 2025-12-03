/**
 * File: vosk.h
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo declara funções auxiliares para carregar o modelo Vosk,
 * criar reconhecedores e processar o áudio capturado.
 */

#ifndef VOSK_HELPER_H
#define VOSK_HELPER_H

#include <string>
#include <jsoncpp/json/json.h>
#include <vosk_api.h>
#include <iostream>

#define VOSK_SAMPLE_RATE    16000 // Não alterar, Vosk usa 16kHz
#define INPUT_SAMPLE_RATE   44100 // Frequência de captura do ALSA

VoskModel* load_vosk_model(const std::string& path);
 
VoskRecognizer* create_command_recognizer(VoskModel* model);

std::string vosk_process(VoskRecognizer* rec);

#endif
