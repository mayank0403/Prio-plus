#include "net_share.h"

#include <sys/socket.h>

#include "fmpz_utils.h"

/* Core functions */

int send_int(const int sockfd, const int x) {
    int x_conv = htonl(x);
    const char* data = (const char*) &x_conv;
    return send(sockfd, data, sizeof(int), 0);
}

int recv_int(const int sockfd, int& x) {
    int bytes_read = 0, tmp;
    char buf[sizeof(int)];
    while (bytes_read < sizeof(int)) {
        tmp = recv(sockfd, &buf + bytes_read, sizeof(int) - bytes_read, 0);
        if (tmp <= 0) return tmp; else bytes_read += tmp;
    }
    x = ntohl(*((int*)buf));
    return bytes_read;
}

int send_ulong(const int sockfd, const ulong x) {
    ulong x_conv = htonll(x);
    const char* data = (const char*) &x_conv;
    return send(sockfd, data, sizeof(ulong), 0);
}

int recv_ulong(const int sockfd, ulong& x) {
    char buf[sizeof(ulong)];
    int bytes_read = 0, tmp;
    while (bytes_read < sizeof(ulong)) {
        tmp = recv(sockfd, &buf + bytes_read, sizeof(ulong) - bytes_read, 0);
        if (tmp <= 0) return tmp; else bytes_read += tmp;
    }
    x = ntohll(*((ulong*)buf));
    return bytes_read;
}

int send_fmpz(const int sockfd, const fmpz_t x) {
    int total = 0, ret;
    size_t len = fmpz_size(x);
    ulong arr[len];
    fmpz_get_ui_array(arr, len, x);
    ret = send_int(sockfd, len);
    if (ret <= 0) return ret; else total += ret;
    for (int i = 0; i < len; i++) {
        ret = send_ulong(sockfd, arr[i]);
        if (ret <= 0) return ret; else total += ret;
    }
    return total;
}

int recv_fmpz(const int sockfd, fmpz_t x) {
    int total = 0, ret, len;
    ulong tmp;
    ret = recv_int(sockfd, len);
    if (ret <= 0) return ret; else total += ret;
    if (len == 0) {
        fmpz_set_ui(x, 0);
        return total;
    }
    ulong buf[len];
    for (int i = 0; i < len; i++) {
        ret = recv_ulong(sockfd, tmp);
        if (ret <= 0) return ret; else total += ret;
        buf[i] = tmp;
    }
    fmpz_set_ui_array(x, buf, len);
    return total;
}

/* Share functions */

int send_Cor(const int sockfd, const Cor *x) {
    int total = 0, ret;
    ret = send_fmpz(sockfd, x->D);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->E);
    if (ret <= 0) return ret; else total += ret;
    return total;
}

int recv_Cor(const int sockfd, Cor *x) {
    int total = 0, ret;
    ret = recv_fmpz(sockfd, x->D);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->E);
    if (ret <= 0) return ret; else total += ret;
    return total;
}

int send_CorShare(const int sockfd, const CorShare *x) {
    int total = 0, ret;
    ret = send_fmpz(sockfd, x->shareD);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->shareE);
    if (ret <= 0) return ret; else total += ret;
    return total;
}

int recv_CorShare(const int sockfd, CorShare *x) {
    int total = 0, ret;
    ret = recv_fmpz(sockfd, x->shareD);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->shareE);
    if (ret <= 0) return ret; else total += ret;
    return total;
}

// ClientPacket = client_packet*
int send_ClientPacket(const int sockfd, const ClientPacket x) {
    int N = x->N, NWires = x->NWires, total = 0, ret;
    ret = send_int(sockfd, N);
    if (ret <= 0) return ret; else total += ret;
    ret = send_int(sockfd, NWires);
    if (ret <= 0) return ret; else total += ret;

    int i;
    for (i = 0; i < NWires; i++) {
        ret = send_fmpz(sockfd, x->WireShares[i]);
        if (ret <= 0) return ret; else total += ret;
    }

    ret = send_fmpz(sockfd, x->f0_s);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->g0_s);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->h0_s);
    if (ret <= 0) return ret; else total += ret;

    for (i = 0; i < N; i++) {
        ret = send_fmpz(sockfd, x->h_points[i]);
        if (ret <= 0) return ret; else total += ret;
    }

    ret = send_BeaverTripleShare(sockfd, x->triple_share);
    if (ret <= 0) return ret; else total += ret;

    return total;
}

int recv_ClientPacket(const int sockfd, ClientPacket &x) {
    int N, NWires, total = 0, ret;
    ret = recv_int(sockfd, N);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_int(sockfd, NWires);
    if (ret <= 0) return ret; else total += ret;
    init_client_packet(x, N, NWires);

    int i;
    for (i = 0; i < NWires; i++) {
        ret = recv_fmpz(sockfd, x->WireShares[i]);
        if (ret <= 0) return ret; else total += ret;
    }

    ret = recv_fmpz(sockfd, x->f0_s);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->g0_s);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->h0_s);
    if (ret <= 0) return ret; else total += ret;

    for (i = 0; i < N; i++) {
        ret = recv_fmpz(sockfd, x->h_points[i]);
        if (ret <= 0) return ret; else total += ret;
    }

    ret = recv_BeaverTripleShare(sockfd, x->triple_share);
    if (ret <= 0) return ret; else total += ret;

    return total;
}

int send_BeaverTriple(const int sockfd, const BeaverTriple *x) {
    int total = 0, ret;
    ret = send_fmpz(sockfd, x->A);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->B);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->C);
    if (ret <= 0) return ret; else total += ret;
    return total;
}

int recv_BeaverTriple(const int sockfd, BeaverTriple *x) {
    int total = 0, ret;
    ret = recv_fmpz(sockfd, x->A);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->B);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->C);
    if (ret <= 0) return ret; else total += ret;
    return total;
}

int send_BeaverTripleShare(const int sockfd, const BeaverTripleShare *x) {
    int total = 0, ret;
    ret = send_fmpz(sockfd, x->shareA);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->shareB);
    if (ret <= 0) return ret; else total += ret;
    ret = send_fmpz(sockfd, x->shareC);
    return total;
}

int recv_BeaverTripleShare(const int sockfd, BeaverTripleShare *x) {
    int total = 0, ret;
    ret = recv_fmpz(sockfd, x->shareA);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->shareB);
    if (ret <= 0) return ret; else total += ret;
    ret = recv_fmpz(sockfd, x->shareC);
    if (ret <= 0) return ret; else total += ret;
    return total;
}