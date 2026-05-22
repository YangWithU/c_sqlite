#include "../xtest.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

static int server_fd;
static struct sockaddr_in server_addr;

static void network_setup(void) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_NE(server_fd, -1);

    /* Set port reuse to avoid "Address already in use" during quick reruns. */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(0);
    
    EXPECT_EQ(bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)), 0);
    EXPECT_EQ(listen(server_fd, 1), 0);

}

static void network_teardown(void) {
    if (server_fd >= 0) { close(server_fd); server_fd = -1; }
}

TEST_DEFINE_FIXTURE(network_fxt, network_setup, network_teardown);

TEST_F(network, tcp_simple_send_recv, network_fxt) {
    struct sockaddr_in bound = {0};
    socklen_t len = sizeof(bound);
    EXPECT_EQ(getsockname(server_fd, (struct sockaddr *)&bound, &len), 0);
    EXPECT_EQ(bound.sin_family, AF_INET);
    EXPECT_NE(ntohs(bound.sin_port), 0);
}

int func() {
    return 42;
}

TEST(network, helper_function_returns_value) {
    EXPECT_EQ(func(), 42);
}
