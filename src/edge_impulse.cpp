/**
 * File: edge_impulse.cpp
 * Autor: Victor Lompa Schwider
 * 
 * Este arquivo implementa funções relacionadas à integração com o Edge Impulse SDK
 */

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge_impulse.h"

/* Detecta a palavra-chave "Zenira" no áudio capturado.
*
*   @param signal Ponteiro para a estrutura signal_t que será preenchida.
*   @return true se a palavra-chave foi detectada, false caso contrário ou em caso de erro.
*/
bool wake_word_detected(signal_t* signal) {
    ei_impulse_result_t result;
    EI_IMPULSE_ERROR res = run_classifier(signal, &result, false);

    if (res != EI_IMPULSE_OK) {
#if LOGS_ERRO_EI
        std::cerr << "[ERRO] Erro ao classificar: " << res << std::endl;
#endif
        return false;
    }

    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        std::string label(result.classification[ix].label);
        float value = result.classification[ix].value;
#if CLASSIFIER_VERBOSE
        std::cout << label << ": " << value << std::endl;
#endif
        if (label == "Zenira" && value > 0.8f) {
#if LOGS_INFO_EI
            std::cout << "[Wake word detectada!]" << std::endl;
#endif
            return true;
        }
    }

    return false;
}