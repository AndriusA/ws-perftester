#include "clients/mobile-signaling-client.hpp"
#include <stdint.h>

int main() {
	mobile_signaling_client<mobile_signaling_conf> client("", 5, true, 1, 256);
	// mobile_signaling_client<mobile_signaling_conf> client2("", 5, true, 1, 256);
	std::cout << "Run!" << std::endl;
    client.run("ws://ec2-54-211-126-143.compute-1.amazonaws.com:9002");
    // client2.run("ws://ec2-54-211-126-143.compute-1.amazonaws.com:9002");
    client.wait();
    // client2.wait();
}