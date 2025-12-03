/**
 * File: vosk.cpp
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo implementa funções auxiliares para carregar modelos Vosk,
 * criar reconhecedores de comandos com gramáticas específicas e processar
 * o reconhecimento de fala utilizando a biblioteca Vosk.
 */

// Includes
#include "vosk.h"

/**
 * Carrega o modelo Vosk a partir do caminho especificado.
 * 
 * @param path Caminho para o diretório do modelo Vosk.
 * @return VoskModel* Ponteiro para o modelo carregado.
 */
VoskModel* load_vosk_model(const std::string& path) {
    return vosk_model_new(path.c_str());
}

/**
*   Cria um reconhecedor de comandos Vosk com um modelo pré-carregado.
*
*   @param model Ponteiro para o modelo Vosk carregado.
*   @return Ponteiro para o reconhecedor de comandos ou nullptr em caso de erro.
*/
VoskRecognizer* create_command_recognizer(VoskModel* model) {
    const char* grammar = R"([
        "mudar velocidade para dez",       "velocidade para dez",       "velocidade dez",
        "mudar velocidade para vinte",     "velocidade para vinte",     "velocidade vinte",
        "mudar velocidade para trinta",    "velocidade para trinta",    "velocidade trinta",
        "mudar velocidade para quarenta",  "velocidade para quarenta",  "velocidade quarenta",
        "mudar velocidade para cinquenta", "velocidade para cinquenta", "velocidade cinquenta",
        "mudar velocidade para sessenta",  "velocidade para sessenta",  "velocidade sessenta",
        "mudar velocidade para setenta",   "velocidade para setenta",   "velocidade setenta",
        "mudar velocidade para oitenta",   "velocidade para oitenta",   "velocidade oitenta",
        "mudar velocidade para noventa",   "velocidade para noventa",   "velocidade noventa",
        "mudar velocidade para cem",       "velocidade para cem",       "velocidade cem",
        "desligar motor", "motor off", "ligar motor", "motor on",
        "desligar barco", "barco off", "ligar barco", "barco on",
        "virar a direita", "direita", "virar a estibordo", "estibordo",
        "virar a esquerda", "esquerda", "virar a bombordo", "bombordo",
        "seguir reto", "reto"
    ])";

    return vosk_recognizer_new_grm(model, INPUT_SAMPLE_RATE, grammar);
}

/**
 * Processa o reconhecimento de fala e retorna o texto reconhecido.
 * 
 * @param rec Ponteiro para o reconhecedor Vosk.
 * @return std::string Texto reconhecido.
 */
std::string vosk_process(VoskRecognizer* rec) {
    std::string out = vosk_recognizer_result(rec);
    Json::Reader reader;
    Json::Value root;
    if (reader.parse(out, root)) return root["text"].asString();
    return "";
}
