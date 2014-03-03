#include "clients/telemetry-client-tls.hpp"
#include <stdint.h>

int main() {
	telemetry_client client("", 5, true, 1, 256);
	telemetry_client client2("", 5, true, 1, 256);
	std::cout << "Run!" << std::endl;
    client.run("wss://ec2-54-211-126-143.compute-1.amazonaws.com:9002");
    client2.run("wss://ec2-54-211-126-143.compute-1.amazonaws.com:9002");
    client.wait();
    client2.wait();
}