#include "can.h"

bool send_can(int sock, uint32_t can_id, const uint8_t* data, uint8_t dlc) {
    struct can_frame frame;
    frame.can_id = can_id;
    frame.can_dlc = dlc;
    std::memcpy(frame.data, data, dlc);

    int nbytes = write(sock, &frame, sizeof(struct can_frame));
    if (nbytes != sizeof(struct can_frame)) {
        perror("[ERRO] envio CAN");
        return false;
    }

    std::cout << "[CAN] Mensagem enviada - ID: 0x" << std::hex << can_id << std::dec << "\n";
    return true;
}

int setup_can(void) {
    int sock;
    struct ifreq ifr;
    struct sockaddr_can addr;

    // Cria socket CAN raw
    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("[ERRO] socket");
        return -1;
    }

    // Define nome da interface (can0)
    std::strcpy(ifr.ifr_name, getenv("CAN_INTERFACE") ? getenv("CAN_INTERFACE") : "can0");
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("[ERRO] ioctl");
        return -1;
    }

    // Associa socket à interface CAN
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ERRO] bind");
        return -1;
    }

    std::cout << "[INFO] Interface CAN configurada com sucesso.\n";
    return sock;
}

bool receive_can(int sock, struct can_frame& frame) {
    int nbytes = read(sock, &frame, sizeof(struct can_frame));
    if (nbytes < 0) {
        perror("[ERRO] leitura CAN");
        return false;
    }

    std::cout << "[CAN] Mensagem recebida - ID: 0x" << std::hex << frame.can_id << std::dec
              << " | Dados: ";
    for (int i = 0; i < frame.can_dlc; ++i)
        std::cout << std::hex << (int)frame.data[i] << " ";
    std::cout << std::dec << "\n";

    return true;
}

void close_can(int sock) {
    if (sock >= 0) {
        close(sock);
        std::cout << "[INFO] Socket CAN fechado com sucesso.\n";
    }
}
