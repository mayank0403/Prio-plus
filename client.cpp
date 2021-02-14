/*
Simulates a group of num_submission clients that communicate with the servers.

General layout:

X_op_helper: Makes then sends a batch of client requests
X_op: Sends init msg, then sends either one batch or a bunch in serial
x_op_invalid: For testing/debugging, does a basic run with intentionally invalid clients.
*/

// TODO: Eventually htonl/ntohl wrappers on shares. Fine when client/server on same machine.

#include "client.h"

#include <arpa/inet.h>
#include <math.h>  // sqrt
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "circuit.h"
#include "net_share.h"
#include "proto.h"
#include "types.h"

// #define SERVER0_IP "52.87.230.64"
// #define SERVER1_IP "54.213.189.18"

#define SERVER0_IP "127.0.0.1"
#define SERVER1_IP "127.0.0.1"


uint64_t max_int;
uint64_t small_max_int; // sqrt(max_int)
int sockfd0, sockfd1;
bool include_invalid = false;
bool do_batch = true;

void error_exit(const char* const msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

std::string pub_key_to_hex(const uint64_t* const key) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(16) << std::hex << key[0];
    ss << std::setfill('0') << std::setw(16) << std::hex << key[1];
    return ss.str();
}

int send_maxshare(const MaxShare& maxshare, const int server_num, const unsigned int B) {
    int sock = (server_num == 0) ? sockfd0 : sockfd1;

    int ret = send(sock, (void *)&maxshare, sizeof(MaxShare), 0);

    for (unsigned int i = 0; i <= B; i++)
        ret += send(sock, (void *)&(maxshare.arr[i]), sizeof(uint32_t), 0);
    return ret;
}

// Wrapper around send, with error catching.
int send_to_server(const int server, const void* const buffer, const size_t n, const int flags = 0) {
    int socket = (server == 0 ? sockfd0 : sockfd1);
    int ret = send(socket, buffer, n, flags);
    if (ret < 0) error_exit("Failed to send to server ");
    return ret;
}

int bit_sum_helper(const std::string protocol, const size_t numreqs,
                   unsigned int &ans, const initMsg* const msg_ptr = nullptr) {
    auto start = clock_start();
    int num_bytes = 0;

    emp::block* const b = new block[numreqs];
    bool* real_vals = new bool[numreqs];
    bool* shares0 = new bool[numreqs];
    bool* shares1 = new bool[numreqs];

    // Can't use a fixed key, or serial will have the same key every time
    emp::PRG prg;
    prg.random_block(b, numreqs);
    prg.random_bool(real_vals, numreqs);
    prg.random_bool(shares0, numreqs);

    BitShare* bitshare0 = new BitShare[numreqs];
    BitShare* bitshare1 = new BitShare[numreqs];
    for (unsigned int i = 0; i < numreqs; i++) {
        const std::string pk_s = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_s.c_str();

        shares1[i] = real_vals[i]^shares0[i];
        ans += (real_vals[i] ? 1 : 0);

        // std::cout << pk << ": " << std::noboolalpha << real_vals[i] << " = " << shares0[i] << " ^ " << shares1[i] << std::endl;

        memcpy(bitshare0[i].pk, &pk[0], PK_LENGTH);
        bitshare0[i].val = shares0[i];
        memcpy(bitshare0[i].signature, &pk[0], PK_LENGTH);

        memcpy(bitshare1[i].pk, &pk[0], PK_LENGTH);
        bitshare1[i].val = shares1[i];
        memcpy(bitshare1[i].signature, &pk[0], PK_LENGTH);
    }
    if (numreqs > 1)
        std::cout << "batch make:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    start = clock_start();
    if (msg_ptr != nullptr) {
        num_bytes += send_to_server(0, msg_ptr, sizeof(initMsg));
        num_bytes += send_to_server(1, msg_ptr, sizeof(initMsg));
    }
    for (unsigned int i = 0; i < numreqs; i++) {
        num_bytes += send_to_server(0, &bitshare0[i], sizeof(BitShare));
        num_bytes += send_to_server(1, &bitshare1[i], sizeof(BitShare));
    }

    delete[] b;
    delete[] real_vals;
    delete[] shares0;
    delete[] shares1;
    delete[] bitshare0;
    delete[] bitshare1;

    if (numreqs > 1)
        std::cout << "batch send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;

    return num_bytes;
}

void bit_sum(const std::string protocol, const size_t numreqs) {
    unsigned int ans = 0;
    int num_bytes = 0;
    initMsg msg;
    msg.num_of_inputs = numreqs;
    msg.type = BIT_SUM;

    if (do_batch) {
        num_bytes += bit_sum_helper(protocol, numreqs, ans, &msg);
    } else {
        auto start = clock_start();
        for (unsigned int i = 0; i < numreqs; i++)
            num_bytes += bit_sum_helper(protocol, 1, ans, i == 0 ? &msg : nullptr);
        std::cout << "make+send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    std::cout << "Uploaded all shares. Ans : " << ans << std::endl;
    std::cout << "Total sent bytes: " << num_bytes << std::endl;
}

/* 0: pk mismatch
   2: share0 has same pk as 1
   4: share1 has same pk as 3
*/
void bit_sum_invalid(const std::string protocol, const size_t numreqs) {
    initMsg msg;
    msg.num_of_inputs = numreqs;
    msg.type = BIT_SUM;
    send_to_server(0, &msg, sizeof(initMsg));
    send_to_server(1, &msg, sizeof(initMsg));

    emp::block* const b = new block[numreqs];
    bool real_vals[numreqs];
    bool shares0[numreqs];
    bool shares1[numreqs];

    emp::PRG prg(fix_key);
    prg.random_block(b, numreqs);
    prg.random_bool(real_vals, numreqs);
    prg.random_bool(shares0, numreqs);

    int ans = 0;
    std::string pk_str = "";
    for (unsigned int i = 0; i < numreqs; i++) {
        BitShare share0, share1;
        const char* prev_pk = pk_str.c_str();
        pk_str = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_str.c_str();

        shares1[i] = real_vals[i]^shares0[i];
        std::cout << i << ": " << std::boolalpha << shares0[i] << " ^ " << shares1[i] << " = " << real_vals[i];
        if (i == 0 or i == 2 or i == 4) {
            std::cout << " (invalid)" << std::endl;
        } else {
            std::cout << std::endl;
            ans += (real_vals[i] ? 1 : 0);
        }

        memcpy(share0.pk, &pk[0], PK_LENGTH);
        share0.val = shares0[i];
        memcpy(share0.signature, &pk[0], PK_LENGTH);
        if (i == 0)
            share0.pk[0] = 'q';
        if (i == 2)
            memcpy(share0.pk, &prev_pk[0], PK_LENGTH);

        memcpy(share1.pk, &pk[0], PK_LENGTH);
        share1.val = shares1[i];
        memcpy(share1.signature, &pk[0], PK_LENGTH);
        if (i == 4)
            memcpy(share1.pk, &prev_pk[0], PK_LENGTH);

        send_to_server(0, &share0, sizeof(BitShare));
        send_to_server(1, &share1, sizeof(BitShare));
    }
    std::cout << "Uploaded all shares. Ans : " << ans << std::endl;

    delete[] b;
}

int int_sum_helper(const std::string protocol, const size_t numreqs,
                   uint64_t &ans, const initMsg* const msg_ptr = nullptr) {
    auto start = clock_start();
    int num_bytes = 0;

    emp::block* const b = new block[numreqs];
    uint64_t* real_vals = new uint64_t[numreqs];
    uint64_t* shares0 = new uint64_t[numreqs];
    uint64_t* shares1 = new uint64_t[numreqs];

    emp::PRG prg;
    prg.random_block(b, numreqs);
    prg.random_data(real_vals, numreqs * sizeof(uint64_t));
    prg.random_data(shares0, numreqs * sizeof(uint64_t));

    IntShare* intshare0 = new IntShare[numreqs];
    IntShare* intshare1 = new IntShare[numreqs];

    for (unsigned int i = 0; i < numreqs; i++) {
        real_vals[i] = real_vals[i] % max_int;
        shares0[i] = shares0[i] % max_int;
        shares1[i] = real_vals[i] ^ shares0[i];
        ans += real_vals[i];

        const std::string pk_s = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_s.c_str();

        memcpy(intshare0[i].pk, &pk[0], PK_LENGTH);
        intshare0[i].val = shares0[i];
        memcpy(intshare0[i].signature, &pk[0], PK_LENGTH);

        memcpy(intshare1[i].pk, &pk[0], PK_LENGTH);
        intshare1[i].val = shares1[i];
        memcpy(intshare1[i].signature, &pk[0], PK_LENGTH);
    }
    if (numreqs > 1)
        std::cout << "batch make:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    start = clock_start();
    if (msg_ptr != nullptr) {
        num_bytes += send_to_server(0, msg_ptr, sizeof(initMsg));
        num_bytes += send_to_server(1, msg_ptr, sizeof(initMsg));
    }
    for (unsigned int i = 0; i < numreqs; i++) {
        num_bytes += send_to_server(0, &intshare0[i], sizeof(IntShare));
        num_bytes += send_to_server(1, &intshare1[i], sizeof(IntShare));
    }
    delete[] intshare0;
    delete[] intshare1;
    delete[] b;
    delete[] real_vals;
    delete[] shares0;
    delete[] shares1;

    if (numreqs > 1)
        std::cout << "batch send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;

    return num_bytes;
}

void int_sum(const std::string protocol, const size_t numreqs) {
    uint64_t ans = 0;
    int num_bytes = 0;
    initMsg msg;
    msg.num_of_inputs = numreqs;
    msg.type = INT_SUM;

    if (do_batch) {
        num_bytes += int_sum_helper(protocol, numreqs, ans, &msg);
    } else {
        auto start = clock_start();
        for (unsigned int i = 0; i < numreqs; i++)
            num_bytes += int_sum_helper(protocol, 1, ans, i == 0 ? &msg : nullptr);
        std::cout << "make+send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    std::cout << "Uploaded all shares. Ans : " << ans << std::endl;
    std::cout << "Total sent bytes: " << num_bytes << std::endl;
}

/* 0: x > max
   1: x share > max
   2: pk mismatch
   4: share0 has same pk as 3
   6: share1 has same pk as 5
*/
void int_sum_invalid(const std::string protocol, const size_t numreqs) {
    initMsg msg;
    msg.num_of_inputs = numreqs;
    msg.type = INT_SUM;
    send_to_server(0, &msg, sizeof(initMsg));
    send_to_server(1, &msg, sizeof(initMsg));

    emp::block* const b = new block[numreqs];
    uint64_t real_vals[numreqs];
    uint64_t shares0[numreqs];
    uint64_t shares1[numreqs];
    uint64_t ans = 0;

    emp::PRG prg(fix_key);
    prg.random_block(b, numreqs);
    prg.random_data(real_vals, numreqs * sizeof(uint64_t));
    prg.random_data(shares0, numreqs * sizeof(uint64_t));

    std::string pk_str = "";
    for (unsigned int i = 0; i < numreqs; i++) {
        if (i != 0)
            real_vals[i] = real_vals[i] % max_int;
        if (i != 1)
            shares0[i] = shares0[i] % max_int;
        shares1[i] = real_vals[i] ^ shares0[i];
        std::cout << "real_vals[" << i << "] = " << real_vals[i] << " = " << shares0[i] << " ^ " << shares1[i];
        if (i <= 2 or i == 4 or i == 6) {
            std::cout << " (invalid)" << std::endl;
        } else {
            std::cout << std::endl;
            ans += real_vals[i];
        }

        IntShare share0, share1;
        const char* prev_pk = pk_str.c_str();
        pk_str = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_str.c_str();

        memcpy(share0.pk, &pk[0], PK_LENGTH);
        share0.val = shares0[i];
        memcpy(share0.signature, &pk[0], PK_LENGTH);
        if (i == 2)
            share0.pk[0] = 'q';
        if (i == 4)
            memcpy(share0.pk, &prev_pk[0], PK_LENGTH);

        memcpy(share1.pk, &pk[0], PK_LENGTH);
        share1.val = shares1[i];
        memcpy(share1.signature, &pk[0], PK_LENGTH);
        if (i == 6)
            memcpy(share1.pk, &prev_pk[0], PK_LENGTH);

        send_to_server(0, &share0, sizeof(IntShare));
        send_to_server(1, &share1, sizeof(IntShare));
    }

    std::cout << "Uploaded all shares. Ans : " << ans << std::endl;

    delete[] b;
}

int xor_op_helper(const std::string protocol, const size_t numreqs,
                  bool &ans, const initMsg* const msg_ptr = nullptr) {
    auto start = clock_start();
    int num_bytes = 0;

    emp::block* const b = new block[numreqs];
    bool* values = new bool[numreqs];
    uint64_t* encoded_values = new uint64_t[numreqs];
    uint64_t* shares0 = new uint64_t[numreqs];
    uint64_t* shares1 = new uint64_t[numreqs];

    emp::PRG prg;
    prg.random_block(b, numreqs);
    prg.random_bool(values, numreqs);
    prg.random_data(encoded_values, numreqs * sizeof(uint64_t));
    prg.random_data(shares0, numreqs * sizeof(uint64_t));

    IntShare* intshare0 = new IntShare[numreqs];
    IntShare* intshare1 = new IntShare[numreqs];
    // encode step. set to all 0's for values that don't force the ans.
    if (protocol == "ANDOP") {
        for (unsigned int i = 0; i < numreqs; i++) {
            ans &= values[i];
            if (values[i])
                encoded_values[i] = 0;
        }
    }
    if (protocol == "OROP") {
        for (unsigned int i = 0; i < numreqs; i++) {
            ans |= values[i];
            if (not values[i])
                encoded_values[i] = 0;
        }
    }

    for (unsigned int i = 0; i < numreqs; i++) {
        shares1[i] = encoded_values[i] ^ shares0[i];

        const std::string pk_s = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_s.c_str();

        memcpy(intshare0[i].pk, &pk[0], PK_LENGTH);
        intshare0[i].val = shares0[i];
        memcpy(intshare0[i].signature, &pk[0], PK_LENGTH);

        memcpy(intshare1[i].pk, &pk[0], PK_LENGTH);
        intshare1[i].val = shares1[i];
        memcpy(intshare1[i].signature, &pk[0], PK_LENGTH);
    }
    if (numreqs > 1)
        std::cout << "batch make:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    start = clock_start();
    if (msg_ptr != nullptr) {
        num_bytes += send_to_server(0, msg_ptr, sizeof(initMsg));
        num_bytes += send_to_server(1, msg_ptr, sizeof(initMsg));
    }
    for (unsigned int i = 0; i < numreqs; i++) {
        num_bytes += send_to_server(0, &intshare0[i], sizeof(IntShare));
        num_bytes += send_to_server(1, &intshare1[i], sizeof(IntShare));
    }

    delete[] intshare0;
    delete[] intshare1;
    delete[] b;
    delete[] values;
    delete[] encoded_values;
    delete[] shares0;
    delete[] shares1;

    if (numreqs > 1)
        std::cout << "batch send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;

    return num_bytes;
}

void xor_op(const std::string protocol, const size_t numreqs) {
    bool ans;
    int num_bytes = 0;
    initMsg msg;
    msg.num_of_inputs = numreqs;
    if (protocol == "ANDOP") {
        msg.type = AND_OP;
        ans = true;
    } else if (protocol == "OROP") {
        msg.type = OR_OP;
        ans = false;
    } else {
        return;
    }

    if (do_batch) {
        num_bytes += xor_op_helper(protocol, numreqs, ans, &msg);
    } else {
        auto start = clock_start();
        for (unsigned int i = 0; i < numreqs; i++)
            num_bytes += xor_op_helper(protocol, 1, ans, i == 0 ? &msg : nullptr);
        std::cout << "make+send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    std::cout << "Uploaded all shares. Ans : " << std::boolalpha << ans << std::endl;
    std::cout << "Total sent bytes: " << num_bytes << std::endl;
}

/* 0: pk mismatch
   2: share0 has same pk as 1
   4: share1 has same pk as 3
*/
void xor_op_invalid(const std::string protocol, const size_t numreqs) {
    initMsg msg;
    msg.num_of_inputs = numreqs;
    bool ans;
    if (protocol == "ANDOP") {
        msg.type = AND_OP;
        ans = true;
    } else if (protocol == "OROP") {
        msg.type = OR_OP;
        ans = false;
    } else {
        return;
    }
    send_to_server(0, &msg, sizeof(initMsg));
    send_to_server(1, &msg, sizeof(initMsg));

    emp::block* const b = new block[numreqs];
    bool values[numreqs];
    uint64_t encoded_values[numreqs];
    uint64_t shares0[numreqs];
    uint64_t shares1[numreqs];

    emp::PRG prg(fix_key);

    prg.random_block(b, numreqs);
    prg.random_bool(values, numreqs);
    prg.random_data(encoded_values, numreqs * sizeof(uint64_t));
    prg.random_data(shares0, numreqs * sizeof(uint64_t));

    for (unsigned int i = 0; i < numreqs; i++) {
        std::cout << "val[" << i << "] = " << std::boolalpha << values[i];
        if (i == 0 or i == 2 or i == 4) {
            std::cout << " (invalid) " << std::endl;
            continue;
        }
        std::cout << std::endl;
        if (protocol == "ANDOP")
            ans &= values[i];
        if (protocol == "OROP")
            ans |= values[i];
    }

    // encode step. set to all 0's for values that don't force the ans.
    if (protocol == "ANDOP")
        for (unsigned int i = 0; i < numreqs; i++)
            if (values[i])
                encoded_values[i] = 0;
    if (protocol == "OROP")
        for (unsigned int i = 0; i < numreqs; i++)
            if (!values[i])
                encoded_values[i] = 0;

    std::string pk_str = "";

    // Share splitting. Same as int sum. Sum of shares = encoded value
    for (unsigned int i = 0; i < numreqs; i++) {
        shares1[i] = encoded_values[i] ^ shares0[i];

        IntShare share0, share1;
        const char* prev_pk = pk_str.c_str();
        pk_str = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_str.c_str();

        memcpy(share0.pk, &pk[0], PK_LENGTH);
        share0.val = shares0[i];
        memcpy(share0.signature, &pk[0], PK_LENGTH);
        if (include_invalid && i == 0)
            share0.pk[0] = 'q';
        if (include_invalid && i == 2)
            memcpy(share0.pk, &prev_pk[0], PK_LENGTH);

        memcpy(share1.pk, &pk[0], PK_LENGTH);
        share1.val = shares1[i];
        memcpy(share1.signature, &pk[0], PK_LENGTH);
        if (include_invalid && i == 4)
            memcpy(share1.pk, &prev_pk[0], PK_LENGTH);

        send_to_server(0, &share0, sizeof(IntShare));
        send_to_server(1, &share1, sizeof(IntShare));
    }

    std::cout << "Uploaded all shares. Ans : " << std::boolalpha << ans << std::endl;

    delete[] b;
}

int max_op_helper(const std::string protocol, const size_t numreqs,
                  const unsigned int B, uint32_t &ans,
                  const initMsg* const msg_ptr = nullptr) {
    auto start = clock_start();
    int num_bytes = 0;

    start = clock_start();

    emp::block* const b = new block[numreqs];
    uint32_t* values = new uint32_t[numreqs];
    uint32_t* or_encoded_array = new uint32_t[B+1];
    uint32_t* shares0 = new uint32_t[B+1];
    uint32_t* shares1 = new uint32_t[B+1];

    emp::PRG prg;
    prg.random_block(b, numreqs);
    prg.random_data(values, numreqs * sizeof(uint32_t));

    MaxShare* maxshare0 = new MaxShare[numreqs];
    MaxShare* maxshare1 = new MaxShare[numreqs];
    for (unsigned int i = 0; i < numreqs; i++) {
        values[i] = values[i] % (B + 1);
        if (protocol == "MAXOP")
            ans = (values[i] > ans? values[i] : ans);
        if (protocol == "MINOP")
            ans = (values[i] < ans? values[i] : ans);

        prg.random_data(or_encoded_array, (B+1)*sizeof(uint32_t));
        prg.random_data(shares0, (B+1)*sizeof(uint32_t));

        uint32_t v = 0;
        if (protocol == "MAXOP")
            v = values[i];
        if (protocol == "MINOP")
            v = B - values[i];

        for (unsigned int j = v + 1; j <= B ; j++)
            or_encoded_array[j] = 0;

        for (unsigned int j = 0; j <= B; j++)
            shares1[j] = shares0[j] ^ or_encoded_array[j];

        const std::string pk_s = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_s.c_str();

        memcpy(maxshare0[i].pk, &pk[0], PK_LENGTH);
        memcpy(maxshare0[i].signature, &pk[0], PK_LENGTH);
        maxshare0[i].arr = new uint32_t[B+1];
        memcpy(maxshare0[i].arr, &shares0[0], (B+1)*sizeof(uint32_t));

        memcpy(maxshare1[i].pk, &pk[0], PK_LENGTH);
        memcpy(maxshare1[i].signature, &pk[0], PK_LENGTH);
        maxshare1[i].arr = new uint32_t[B+1];
        memcpy(maxshare1[i].arr, &shares1[0], (B+1)*sizeof(uint32_t));
    }
    if (numreqs > 1)
        std::cout << "batch make:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    start = clock_start();
    if (msg_ptr != nullptr) {
        num_bytes += send_to_server(0, msg_ptr, sizeof(initMsg));
        num_bytes += send_to_server(1, msg_ptr, sizeof(initMsg));
    }
    for (unsigned int i = 0; i < numreqs; i++) {
        num_bytes += send_maxshare(maxshare0[i], 0, B);
        num_bytes += send_maxshare(maxshare1[i], 1, B);

        delete[] maxshare0[i].arr;
        delete[] maxshare1[i].arr;
    }

    delete[] maxshare0;
    delete[] maxshare1;
    delete[] b;
    delete[] values;
    delete[] or_encoded_array;
    delete[] shares0;
    delete[] shares1;

    if (numreqs > 1)
        std::cout << "batch send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;

    return num_bytes;
}

void max_op(const std::string protocol, const size_t numreqs) {
    const unsigned int B = 250;

    uint32_t ans;
    int num_bytes = 0;
    initMsg msg;
    msg.num_of_inputs = numreqs;
    msg.max_inp = B;
    if (protocol == "MAXOP") {
        msg.type = MAX_OP;
        ans = 0;
    } else if (protocol == "MINOP") {
        msg.type = MIN_OP;
        ans = B;
    } else {
        return;
    }

    if (do_batch) {
        num_bytes += max_op_helper(protocol, numreqs, B, ans, &msg);
    } else {
        auto start = clock_start();
        for (unsigned int i = 0; i < numreqs; i++)
            num_bytes += max_op_helper(protocol, 1, B, ans, i == 0 ? &msg : nullptr);
        std::cout << "make+send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    std::cout << "Uploaded all shares. Ans : " << ans << std::endl;
    std::cout << "Total sent bytes: " << num_bytes << std::endl;
}

/* 0: pk mismatch
   2: share0 has same pk as 1
   4: share1 has same pk as 3
*/
void max_op_invalid(const std::string protocol, const size_t numreqs) {
    const unsigned int B = 250;
    initMsg msg;
    msg.num_of_inputs = numreqs;
    msg.max_inp = B;
    emp::PRG prg(fix_key);
    uint32_t ans;
    if (protocol == "MAXOP") {
        msg.type = MAX_OP;
        ans = 0;
    } else if (protocol == "MINOP") {
        msg.type = MIN_OP;
        ans = B;
    } else {
        return;
    }

    emp::block* const b = new block[numreqs];
    prg.random_block(b, numreqs);

    uint32_t values[numreqs];
    uint32_t or_encoded_array[B+1];
    uint32_t shares0[B+1];
    uint32_t shares1[B+1];
    prg.random_data(values, numreqs * sizeof(uint32_t));

    send_to_server(0, &msg, sizeof(initMsg), 0);
    send_to_server(1, &msg, sizeof(initMsg), 0);

    std::string pk_str = "";

    for (unsigned int i = 0; i < numreqs; i++) {
        MaxShare share0, share1;
        values[i] = values[i] % (B + 1);
        std::cout << "value[" << i << "] = " << values[i];
        if (i == 0 or i == 2 or i == 4) {
            std::cout << " (invalid)" << std::endl;
        } else {
            std::cout << std::endl;
            if (protocol == "MAXOP")
                ans = (values[i] > ans? values[i] : ans);
            if (protocol == "MINOP")
                ans = (values[i] < ans? values[i] : ans);
        }

        prg.random_data(or_encoded_array, (B+1)*sizeof(uint32_t));
        prg.random_data(shares0, (B+1)*sizeof(uint32_t));

        // min(x) = -max(-x) = B - max(B - x)
        uint32_t v = 0;
        if (protocol == "MAXOP")
            v = values[i];
        if (protocol == "MINOP")
            v = B - values[i];

        for (unsigned int j = v + 1; j <= B ; j++)
            or_encoded_array[j] = 0;

        for (unsigned int j = 0; j <= B; j++)
            shares1[j] = shares0[j] ^ or_encoded_array[j];

        const char* prev_pk = pk_str.c_str();
        pk_str = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_str.c_str();

        memcpy(share0.pk, &pk[0], PK_LENGTH);
        memcpy(share0.signature, &pk[0], PK_LENGTH);
        share0.arr = shares0;
        if (i == 0)
            share0.pk[0] = 'q';
        if (i == 2)
            memcpy(share0.pk, &prev_pk[0], PK_LENGTH);

        memcpy(share1.pk, &pk[0], PK_LENGTH);
        memcpy(share1.signature, &pk[0], PK_LENGTH);
        share1.arr = shares1;
        if (i == 4)
            memcpy(share1.pk, &prev_pk[0], PK_LENGTH);

        send_maxshare(share0, 0, B);
        send_maxshare(share1, 1, B);
    }

    std::cout << "Uploaded all shares. Ans : " << ans << std::endl;

    delete[] b;
}

int var_op_helper(const std::string protocol, const size_t numreqs,
                   uint64_t& sum, uint64_t& sumsquared,
                   const initMsg* const msg_ptr = nullptr) {
    auto start = clock_start();
    int num_bytes = 0;

    emp::block* const b = new block[numreqs];
    uint64_t* real_vals = new uint64_t[numreqs];
    uint64_t* shares0 = new uint64_t[numreqs];
    uint64_t* shares1 = new uint64_t[numreqs];
    uint64_t* shares0_squared = new uint64_t[numreqs];
    uint64_t* shares1_squared = new uint64_t[numreqs];

    fmpz_t inp[2];
    fmpz_init(inp[0]);
    fmpz_init(inp[1]);

    emp::PRG prg;
    prg.random_block(b, numreqs);
    prg.random_data(real_vals, numreqs * sizeof(uint64_t));
    prg.random_data(shares0, numreqs * sizeof(uint64_t));
    prg.random_data(shares0_squared, numreqs * sizeof(uint64_t));

    VarShare* varshare0 = new VarShare[numreqs];
    VarShare* varshare1 = new VarShare[numreqs];
    ClientPacket** packet0 = new ClientPacket*[numreqs];
    ClientPacket** packet1 = new ClientPacket*[numreqs];

    for (unsigned int i = 0; i < numreqs; i++) {
        real_vals[i] = real_vals[i] % small_max_int;
        shares0[i] = shares0[i] % small_max_int;
        shares1[i] = real_vals[i] ^ shares0[i];
        uint64_t squared = real_vals[i] * real_vals[i];
        shares0_squared[i] = shares0_squared[i] % max_int;
        shares1_squared[i] = squared ^ shares0_squared[i];
        sum += real_vals[i];
        sumsquared += squared;

        const std::string pk_s = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_s.c_str();

        memcpy(varshare0[i].pk, &pk[0], PK_LENGTH);
        varshare0[i].val = shares0[i];
        varshare0[i].val_squared = shares0_squared[i];
        memcpy(varshare0[i].signature, &pk[0], PK_LENGTH);

        memcpy(varshare1[i].pk, &pk[0], PK_LENGTH);
        varshare1[i].val = shares1[i];
        varshare1[i].val_squared = shares1_squared[i];
        memcpy(varshare1[i].signature, &pk[0], PK_LENGTH);

        fmpz_set_si(inp[0], real_vals[i]);
        fmpz_set_si(inp[1], real_vals[i] * real_vals[i]);
        Circuit* circuit = CheckVar();
        circuit->Eval(inp);
        packet0[i] = new ClientPacket(circuit->N(), circuit->NumMulInpGates());
        packet1[i] = new ClientPacket(circuit->N(), circuit->NumMulInpGates());
        share_polynomials(circuit, packet0[i], packet1[i]);
        delete circuit;
    }

    if (numreqs > 1)
        std::cout << "batch make:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    start = clock_start();
    if (msg_ptr != nullptr) {
        num_bytes += send_to_server(0, msg_ptr, sizeof(initMsg));
        num_bytes += send_to_server(1, msg_ptr, sizeof(initMsg));
    }
    for (unsigned int i = 0; i < numreqs; i++) {
        num_bytes += send_to_server(0, &varshare0[i], sizeof(VarShare));
        num_bytes += send_to_server(1, &varshare1[i], sizeof(VarShare));

        num_bytes += send_ClientPacket(sockfd0, packet0[i]);
        num_bytes += send_ClientPacket(sockfd1, packet1[i]);

        delete packet0[i];
        delete packet1[i];
    }

    delete[] varshare0;
    delete[] varshare1;
    delete[] packet0;
    delete[] packet1;
    delete[] b;
    delete[] real_vals;
    delete[] shares0;
    delete[] shares1;
    delete[] shares0_squared;
    delete[] shares1_squared;
    fmpz_clear(inp[0]);
    fmpz_clear(inp[1]);

    if (numreqs > 1)
        std::cout << "batch send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;

    return num_bytes;
}

void var_op(const std::string protocol, const size_t numreqs) {
    uint64_t sum = 0, sumsquared = 0;
    int num_bytes = 0;
    initMsg msg;
    msg.num_of_inputs = numreqs;
    if (protocol == "VAROP") {
        msg.type = VAR_OP;
    } else if (protocol == "STDDEVOP") {
        msg.type = STDDEV_OP;
    } else {
        return;
    }

    if (do_batch) {
        num_bytes += var_op_helper(protocol, numreqs, sum, sumsquared, &msg);
    } else {
        auto start = clock_start();
        for (unsigned int i = 0; i < numreqs; i++)
            num_bytes += var_op_helper(protocol, 1, sum, sumsquared, i == 0 ? &msg : nullptr);
        std::cout << "make+send:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    const double ex = 1. * sum / numreqs;
    const double ex2 = 1. * sumsquared / numreqs;
    double ans = ex2 - (ex * ex);
    std::cout << "E[X^2] - E[X]^2 = " << ex2 << " - (" << ex << ")^2 = " << ans << std::endl;
    if (protocol == "STDDEVOP")
        ans = sqrt(ans);
    std::cout << "True Ans: " << ans << std::endl;
    std::cout << "Total sent bytes: " << num_bytes << std::endl;
}

/* 0: x > max
   1: x shares > max
   2: x^2 shares > max
   3: x^2 = x * x + junk, run through snip
   4: x^2 = x * x + junk, run snip with correct x^2  NOT CAUGHT, Server doesn't check equality
   5: p0 N corruption
   6: p1 const value corruption
   7: p0 wire corruption
   8: p1 triple corruption
   9: pk mismatch
   11: share0 has same pk as 10
   13: share1 has same pk as 12
*/
void var_op_invalid(const std::string protocol, const size_t numreqs) {
    initMsg msg;
    msg.num_of_inputs = numreqs;
    if (protocol == "VAROP") {
        msg.type = VAR_OP;
    } else if (protocol == "STDDEVOP") {
        msg.type = STDDEV_OP;
    } else {
        return;
    }
    send_to_server(0, &msg, sizeof(initMsg));
    send_to_server(1, &msg, sizeof(initMsg));

    emp::block* const b = new block[numreqs];
    uint64_t real_vals[numreqs];
    // shares of x
    uint64_t shares0[numreqs];
    uint64_t shares1[numreqs];
    // shares of x^2
    uint64_t shares0_squared[numreqs];
    uint64_t shares1_squared[numreqs];
    uint64_t sum = 0, sumsquared = 0, numvalid = 0;

    emp::PRG prg(fix_key);
    prg.random_block(b, numreqs);
    prg.random_data(real_vals, numreqs * sizeof(uint64_t));
    prg.random_data(shares0, numreqs * sizeof(uint64_t));
    prg.random_data(shares0_squared, numreqs * sizeof(uint64_t));

    std::cout << "small_max_int: " << small_max_int << std::endl;
    std::cout << "max_int: " << max_int << std::endl;

    fmpz_t inp[2];
    fmpz_init(inp[0]);
    fmpz_init(inp[1]);

    std::string pk_str = "";

    for (unsigned int i = 0; i < numreqs; i++) {
        real_vals[i] = real_vals[i] % small_max_int;
        if (i == 0)  // x over cap
            real_vals[i] += small_max_int;
        shares0[i] = shares0[i] % small_max_int;
        if (i == 1)  // x shares over capped
            shares0[i] += small_max_int;
        shares1[i] = real_vals[i] ^ shares0[i];
        uint64_t squared = real_vals[i] * real_vals[i];
        if (i == 3 or i == 4)  // x^2 != x * x
            squared = (squared + 10) % max_int;
        if (i != 2)  // x^2 not capped
            shares0_squared[i] = shares0_squared[i] % max_int;
        shares1_squared[i] = squared ^ shares0_squared[i];

        std::cout << i << ": " << real_vals[i] << " = " << shares0[i] << "^" << shares1[i];
        std::cout << ", " << squared << " = " << shares0_squared[i] << "^" << shares1_squared[i];
        if (i <= 9 or i == 11 or i == 13) {
            std::cout << " (invalid)" << std::endl;
        } else {
            std::cout << std::endl;
            sum += real_vals[i];
            sumsquared += squared;
            numvalid++;
        }

        VarShare share0, share1;
        const char* prev_pk = pk_str.c_str();
        pk_str = pub_key_to_hex((uint64_t*)&b[i]);
        const char* pk = pk_str.c_str();

        memcpy(share0.pk, &pk[0], PK_LENGTH);
        share0.val = shares0[i];
        share0.val_squared = shares0_squared[i];
        memcpy(share0.signature, &pk[0], PK_LENGTH);
        if (i == 9)
            share0.pk[0] = 'q';
        if (i == 11)
            memcpy(share0.pk, &prev_pk[0], PK_LENGTH);

        memcpy(share1.pk, &pk[0], PK_LENGTH);
        share1.val = shares1[i];
        share1.val_squared = shares1_squared[i];
        memcpy(share1.signature, &pk[0], PK_LENGTH);
        if (i == 13)
            memcpy(share1.pk, &prev_pk[0], PK_LENGTH);

        send_to_server(0, &share0, sizeof(VarShare));
        send_to_server(1, &share1, sizeof(VarShare));
        // SNIP: proof that x^2 = x_squared
        fmpz_set_si(inp[0], real_vals[i]);
        fmpz_set_si(inp[1], real_vals[i] * real_vals[i]);
        if (i == 3)
            fmpz_set_si(inp[1], (real_vals[i] * real_vals[i] + 10) % max_int);
        Circuit* circuit = CheckVar();
        // Run through circuit to set wires
        circuit->Eval(inp);
        ClientPacket* p0;
        if (i == 5) {
            p0 = new ClientPacket(1, circuit->NumMulInpGates());
        } else {
            p0 = new ClientPacket(circuit->N(), circuit->NumMulInpGates());
        }
        ClientPacket* p1 = new ClientPacket(circuit->N(), circuit->NumMulInpGates());
        share_polynomials(circuit, p0, p1);
        delete circuit;
        if (i == 6)
            fmpz_add_si(p1->f0_s, p1->f0_s, 1);
        if (i == 7)
            fmpz_add_si(p0->WireShares[0], p0->WireShares[0], 1);
        if (i == 8)
            fmpz_add_si(p1->triple_share->shareA, p1->triple_share->shareA, 1);
        send_ClientPacket(sockfd0, p0);
        send_ClientPacket(sockfd1, p1);
        delete p0;
        delete p1;
    }

    std::cout << "sum: " << sum << std::endl;
    std::cout << "sumsquared: " << sumsquared << std::endl;
    std::cout << "numvalid: " << numvalid << std::endl;

    const double ex = 1. * sum / numvalid;
    const double ex2 = 1. * sumsquared / numvalid;
    double ans = ex2 - (ex * ex);
    std::cout << "E[X^2] - E[X]^2 = " << ex2 << " - (" << ex << ")^2 = " << ans << std::endl;
    if (protocol == "STDDEVOP")
        ans = sqrt(ans);
    std::cout << "True Ans: " << ans << std::endl;

    delete[] b;
    fmpz_clear(inp[0]);
    fmpz_clear(inp[1]);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "Usage: ./bin/client num_submissions server0_port server1_port OPERATION num_bits do_batch include_invalid" << endl;
    }

    const int numreqs = atoi(argv[1]);  // Number of simulated clients
    const int port0 = atoi(argv[2]);
    const int port1 = atoi(argv[3]);

    const std::string protocol(argv[4]);

    if (argc >= 6) {
        int num_bits = atoi(argv[5]);
        max_int = 1ULL << num_bits;
        small_max_int = 1ULL << (num_bits / 2);
        if (num_bits > 63) {
            error_exit("Num bits is too large. Int math is done mod 2^64.");
        }
    }

    if (argc >= 7) {
        std::stringstream ss(argv[6]);
        if (!(ss >> std::boolalpha >> do_batch))
            error_exit("Could not parse to bool");
    }
    std::cout << "Doing batching: " << std::boolalpha << do_batch << std::endl;

    if (argc >= 8) {
        std::stringstream ss(argv[7]);
        if (!(ss >> std::boolalpha >> include_invalid))
            error_exit("Could not parse to bool");
    }
    std::cout << "Include Invalid: " << std::boolalpha << include_invalid << std::endl;

    // Set up server connections

    struct sockaddr_in server1, server0;

    sockfd0 = socket(AF_INET, SOCK_STREAM, 0);
    sockfd1 = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd0 < 0 or sockfd1 < 0) error_exit("Socket creation failed!");
    int sockopt = 0;
    if (setsockopt(sockfd0, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)))
        error_exit("Sockopt on 0 failed");
    if (setsockopt(sockfd1, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)))
        error_exit("Sockopt on 1 failed");
    if (setsockopt(sockfd0, SOL_SOCKET, SO_REUSEPORT, &sockopt, sizeof(sockopt)))
        error_exit("Sockopt on 0 failed");
    if (setsockopt(sockfd1, SOL_SOCKET, SO_REUSEPORT, &sockopt, sizeof(sockopt)))
        error_exit("Sockopt on 1 failed");

    server1.sin_port = htons(port1);
    server0.sin_port = htons(port0);

    server0.sin_family = AF_INET;
    server1.sin_family = AF_INET;

    inet_pton(AF_INET, SERVER0_IP, &server0.sin_addr);
    inet_pton(AF_INET, SERVER1_IP, &server1.sin_addr);
    std::cout << "Connecting to server 0" << std::endl;
    if (connect(sockfd0, (sockaddr*)&server0, sizeof(server0)) < 0)
        error_exit("Can't connect to server0");
    std::cout << "Connecting to server 1" << std::endl;
    if (connect(sockfd1, (sockaddr*)&server1, sizeof(server1)) < 0)
        error_exit("Can't connect to server1");

    init_constants();

    auto start = clock_start();
    if (protocol == "BITSUM") {
        std::cout << "Uploading all BITSUM shares: " << numreqs << std::endl;
        if (include_invalid)
            bit_sum_invalid(protocol, numreqs);
        else
            bit_sum(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "INTSUM") {
        std::cout << "Uploading all INTSUM shares: " << numreqs << std::endl;
        if (include_invalid)
            int_sum_invalid(protocol, numreqs);
        else
            int_sum(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "ANDOP") {
        std::cout << "Uploading all AND shares: " << numreqs << std::endl;
        if (include_invalid)
            xor_op_invalid(protocol, numreqs);
        else
            xor_op(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "OROP") {
        std::cout << "Uploading all OR shares: " << numreqs << std::endl;
        if (include_invalid)
            xor_op_invalid(protocol, numreqs);
        else
            xor_op(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "MAXOP") {
        std::cout << "Uploading all MAX shares: " << numreqs << std::endl;
        if (include_invalid)
            max_op_invalid(protocol, numreqs);
        else
            max_op(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "MINOP") {
        // Min(x) = - max(-x) = b - max(b - x)
        std::cout << "Uploading all MIN shares: " << numreqs << std::endl;
        if (include_invalid)
            max_op_invalid(protocol, numreqs);
        else
            max_op(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "VAROP") {
        std::cout << "Uploading all VAR shares: " << numreqs << std::endl;
        if (include_invalid)
            var_op_invalid(protocol, numreqs);
        else
            var_op(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if (protocol == "STDDEVOP") {
        // Stddev(x) = sqrt(Var(x))
        std::cout << "Uploading all STDDEV shares: " << numreqs << std::endl;
        if (include_invalid)
            var_op_invalid(protocol, numreqs);
        else
            var_op(protocol, numreqs);
        std::cout << "Total time:\t" << (((float)time_from(start))/CLOCKS_PER_SEC) << std::endl;
    }

    else if(protocol == "LINREGOP") {

    }

    else {
        std::cout << "Unrecognized protocol: " << protocol << std::endl;
        initMsg msg;
        msg.type = NONE_OP;
        send_to_server(0, &msg, sizeof(initMsg));
        send_to_server(1, &msg, sizeof(initMsg));
    }

    close(sockfd0);
    close(sockfd1);

    clear_constants();

    return 0;
}
