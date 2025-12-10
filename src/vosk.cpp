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

/** Construtor da classe Vosk. 
 */
Vosk::Vosk() {
}

/** Destrutor da classe Vosk. 
 */
Vosk::~Vosk() {
}

/** Carrega o modelo Vosk a partir do caminho especificado.
 * 
 * @param path Caminho para o diretório do modelo Vosk.
 * @return VoskModel* Ponteiro para o modelo carregado.
 */
VoskModel* Vosk::load_vosk_model(const std::string& path) {
    return vosk_model_new(path.c_str());
}

/** Cria um reconhecedor de comandos Vosk com um modelo pré-carregado.
*
*   @param model Ponteiro para o modelo Vosk carregado.
*   @return Ponteiro para o reconhecedor de comandos ou nullptr em caso de erro.
*/
VoskRecognizer* Vosk::create_command_recognizer(VoskModel* model) {
    const char* grammar = R"([
        "mudar velocidade para zero",      "velocidade para zero",      "velocidade zero",
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

        "virar dez graus a direita", "dez graus a direita",
        "virar dez graus a estibordo", "dez graus a estibordo",
        "virar vinte graus a direita", "vinte graus a direita",
        "virar vinte graus a estibordo", "vinte graus a estibordo",
        "virar trinta graus a direita", "trinta graus a direita",
        "virar trinta graus a estibordo", "trinta graus a estibordo",
        "virar quarenta graus a direita", "quarenta graus a direita",
        "virar quarenta graus a estibordo", "quarenta graus a estibordo",
        "virar cinquenta graus a direita", "cinquenta graus a direita",
        "virar cinquenta graus a estibordo", "cinquenta graus a estibordo",
        "virar sessenta graus a direita", "sessenta graus a direita",
        "virar sessenta graus a estibordo", "sessenta graus a estibordo",
        "virar setenta graus a direita", "setenta graus a direita",
        "virar setenta graus a estibordo", "setenta graus a estibordo",
        "virar oitenta graus a direita", "oitenta graus a direita",
        "virar oitenta graus a estibordo", "oitenta graus a estibordo",
        "virar noventa graus a direita", "noventa graus a direita",
        "virar noventa graus a estibordo", "noventa graus a estibordo",

        "virar dez graus a esquerda", "dez graus a esquerda",
        "virar dez graus a bombordo", "dez graus a bombordo",
        "virar vinte graus a esquerda", "vinte graus a esquerda",
        "virar vinte graus a bombordo", "vinte graus a bombordo",
        "virar trinta graus a esquerda", "trinta graus a esquerda",
        "virar trinta graus a bombordo", "trinta graus a bombordo",
        "virar quarenta graus a esquerda", "quarenta graus a esquerda",
        "virar quarenta graus a bombordo", "quarenta graus a bombordo",
        "virar cinquenta graus a esquerda", "cinquenta graus a esquerda",
        "virar cinquenta graus a bombordo", "cinquenta graus a bombordo",
        "virar sessenta graus a esquerda", "sessenta graus a esquerda",
        "virar sessenta graus a bombordo", "sessenta graus a bombordo",
        "virar setenta graus a esquerda", "setenta graus a esquerda",
        "virar setenta graus a bombordo", "setenta graus a bombordo",
        "virar oitenta graus a esquerda", "oitenta graus a esquerda",
        "virar oitenta graus a bombordo", "oitenta graus a bombordo",
        "virar noventa graus a esquerda", "noventa graus a esquerda",
        "virar noventa graus a bombordo", "noventa graus a bombordo",

        "seguir reto", "reto", "centralizar"
    ])";

    return vosk_recognizer_new_grm(model, INPUT_SAMPLE_RATE, grammar);
}

/** Processa o reconhecimento de fala e retorna o texto reconhecido.
 * 
 * @param rec Ponteiro para o reconhecedor Vosk.
 * @return std::string Texto reconhecido.
 */
std::string Vosk::vosk_process(VoskRecognizer* rec) {
    std::string out = vosk_recognizer_result(rec);
    Json::Reader reader;
    Json::Value root;
    if (reader.parse(out, root)) return root["text"].asString();
    return "";
}
