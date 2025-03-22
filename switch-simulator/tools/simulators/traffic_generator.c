/**
 * @file traffic_generator.c
 * @brief Traffic generator for switch simulator testing
 *
 * This module generates various types of network traffic patterns to test
 * the switch simulator under different load conditions and scenarios.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/ether.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <pthread.h>

/* Configuration parameters */
#define MAX_PACKET_SIZE 9000
#define MIN_PACKET_SIZE 64
#define DEFAULT_PACKET_SIZE 512
#define MAX_THREADS 16

/* Traffic patterns */
typedef enum {
    TRAFFIC_CONSTANT,     /* Constant bit rate */
    TRAFFIC_BURST,        /* Bursts of packets followed by silence */
    TRAFFIC_RANDOM,       /* Random intervals */
    TRAFFIC_RAMP_UP,      /* Gradually increasing rate */
    TRAFFIC_RAMP_DOWN,    /* Gradually decreasing rate */
    TRAFFIC_SINE_WAVE     /* Sinusoidal pattern */
} traffic_pattern_t;

/* Protocol types */
typedef enum {
    PROTO_ETH_RAW,        /* Raw Ethernet frames */
    PROTO_IP_UDP,         /* IP/UDP packets */
    PROTO_IP_TCP,         /* IP/TCP packets */
    PROTO_VLAN,           /* VLAN tagged frames */
    PROTO_ARP             /* ARP packets */
} protocol_type_t;

/* Traffic generator configuration */
typedef struct {
    char interface[IFNAMSIZ];          /* Interface name to send traffic on */
    unsigned int packet_size;          /* Size of each packet */
    unsigned long packets_per_sec;     /* Rate of packet transmission */
    traffic_pattern_t pattern;         /* Traffic pattern to use */
    protocol_type_t protocol;          /* Protocol to use */
    unsigned int duration;             /* Duration in seconds (0 = infinite) */
    unsigned int num_threads;          /* Number of generator threads */
    
    /* Destination information */
    struct ether_addr dst_mac;         /* Destination MAC address */
    struct ether_addr src_mac;         /* Source MAC address */
    struct in_addr dst_ip;             /* Destination IP address */
    struct in_addr src_ip;             /* Source IP address */
    uint16_t dst_port;                 /* Destination port */
    uint16_t src_port;                 /* Source port */
    
    /* VLAN-specific fields */
    uint16_t vlan_id;                  /* VLAN ID (if using VLAN protocol) */
    uint8_t vlan_priority;             /* VLAN priority */
    
    /* Statistics */
    unsigned long packets_sent;        /* Number of packets sent */
    unsigned long bytes_sent;          /* Number of bytes sent */
} traffic_config_t;

/* Global variables */
volatile sig_atomic_t keep_running = 1;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
traffic_config_t global_config;

/**
 * Calculate IP/TCP/UDP checksum
 */
uint16_t calculate_checksum(uint16_t *buf, int nwords) {
    uint32_t sum = 0;
    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

/**
 * Build a raw Ethernet packet
 */
int build_ethernet_packet(unsigned char *buffer, traffic_config_t *config) {
    struct ether_header *eth = (struct ether_header *)buffer;
    
    /* Set Ethernet header */
    memcpy(eth->ether_dhost, &config->dst_mac, ETH_ALEN);
    memcpy(eth->ether_shost, &config->src_mac, ETH_ALEN);
    
    /* Set EtherType based on protocol */
    switch (config->protocol) {
        case PROTO_IP_UDP:
        case PROTO_IP_TCP:
            eth->ether_type = htons(ETH_P_IP);
            break;
        case PROTO_VLAN:
            eth->ether_type = htons(0x8100);  /* 802.1Q VLAN */
            break;
        case PROTO_ARP:
            eth->ether_type = htons(ETH_P_ARP);
            break;
        case PROTO_ETH_RAW:
        default:
            eth->ether_type = htons(0x0800);  /* Default to IP */
            break;
    }
    
    return sizeof(struct ether_header);
}

/**
 * Build a VLAN tag
 */
int build_vlan_tag(unsigned char *buffer, traffic_config_t *config, int offset) {
    uint16_t *tci = (uint16_t *)(buffer + offset);
    uint16_t *ethertype = (uint16_t *)(buffer + offset + 2);
    
    /* Set Tag Control Information (PCP[3] + CFI[1] + VID[12]) */
    *tci = htons((config->vlan_priority << 13) | config->vlan_id);
    
    /* Set EtherType after VLAN tag */
    *ethertype = htons(ETH_P_IP);  /* Default to IP */
    
    return 4;  /* VLAN tag is 4 bytes */
}

/**
 * Build an IP header
 */
int build_ip_header(unsigned char *buffer, traffic_config_t *config, int offset, int data_len) {
    struct iphdr *ip = (struct iphdr *)(buffer + offset);
    
    /* Set IP header */
    ip->version = 4;
    ip->ihl = 5;  /* 5 * 4 = 20 bytes */
    ip->tos = 0;
    ip->tot_len = htons(sizeof(struct iphdr) + data_len);
    ip->id = htons(rand() & 0xFFFF);
    ip->frag_off = 0;
    ip->ttl = 64;
    
    /* Set protocol */
    if (config->protocol == PROTO_IP_UDP) {
        ip->protocol = IPPROTO_UDP;
    } else if (config->protocol == PROTO_IP_TCP) {
        ip->protocol = IPPROTO_TCP;
    } else {
        ip->protocol = IPPROTO_RAW;
    }
    
    ip->check = 0;  /* Will calculate later */
    ip->saddr = config->src_ip.s_addr;
    ip->daddr = config->dst_ip.s_addr;
    
    /* Calculate IP header checksum */
    ip->check = calculate_checksum((uint16_t *)ip, sizeof(struct iphdr) / 2);
    
    return sizeof(struct iphdr);
}

/**
 * Build a UDP header
 */
int build_udp_header(unsigned char *buffer, traffic_config_t *config, int offset, int data_len) {
    struct udphdr *udp = (struct udphdr *)(buffer + offset);
    
    /* Set UDP header */
    udp->source = htons(config->src_port);
    udp->dest = htons(config->dst_port);
    udp->len = htons(sizeof(struct udphdr) + data_len);
    udp->check = 0;  /* Optional for IPv4 */
    
    return sizeof(struct udphdr);
}

/**
 * Build a TCP header
 */
int build_tcp_header(unsigned char *buffer, traffic_config_t *config, int offset) {
    struct tcphdr *tcp = (struct tcphdr *)(buffer + offset);
    
    /* Set TCP header */
    tcp->source = htons(config->src_port);
    tcp->dest = htons(config->dst_port);
    tcp->seq = htonl(rand());
    tcp->ack_seq = 0;
    tcp->doff = 5;  /* 5 * 4 = 20 bytes */
    tcp->fin = 0;
    tcp->syn = 1;  /* SYN packet */
    tcp->rst = 0;
    tcp->psh = 0;
    tcp->ack = 0;
    tcp->urg = 0;
    tcp->window = htons(5840);
    tcp->check = 0;  /* Will calculate later */
    tcp->urg_ptr = 0;
    
    /* No checksum calculation for simplified traffic generator */
    
    return sizeof(struct tcphdr);
}

/**
 * Build an ARP packet
 */
int build_arp_packet(unsigned char *buffer, traffic_config_t *config, int offset) {
    struct arphdr {
        uint16_t hw_type;
        uint16_t proto_type;
        uint8_t hw_addr_len;
        uint8_t proto_addr_len;
        uint16_t operation;
        uint8_t sender_hw_addr[ETH_ALEN];
        uint32_t sender_proto_addr;
        uint8_t target_hw_addr[ETH_ALEN];
        uint32_t target_proto_addr;
    } __attribute__((packed)) *arp;
    
    arp = (struct arphdr *)(buffer + offset);
    
    /* Set ARP header */
    arp->hw_type = htons(1);  /* Ethernet */
    arp->proto_type = htons(ETH_P_IP);  /* IPv4 */
    arp->hw_addr_len = ETH_ALEN;  /* 6 bytes */
    arp->proto_addr_len = 4;  /* IPv4 = 4 bytes */
    arp->operation = htons(1);  /* ARP request */
    
    memcpy(arp->sender_hw_addr, &config->src_mac, ETH_ALEN);
    arp->sender_proto_addr = config->src_ip.s_addr;
    
    memset(arp->target_hw_addr, 0, ETH_ALEN);  /* Unknown target MAC */
    arp->target_proto_addr = config->dst_ip.s_addr;
    
    return sizeof(struct arphdr);
}

/**
 * Fill packet with random payload data
 */
void fill_random_payload(unsigned char *buffer, int offset, int payload_size) {
    unsigned char *payload = buffer + offset;
    
    /* Fill with pattern or random data */
    for (int i = 0; i < payload_size; i++) {
        payload[i] = (unsigned char)(rand() % 256);
    }
}

/**
 * Build a complete packet according to configuration
 */
int build_packet(unsigned char *buffer, traffic_config_t *config) {
    int offset = 0;
    int payload_size = 0;
    
    /* Add Ethernet header */
    offset += build_ethernet_packet(buffer, config);
    
    /* Add VLAN tag if needed */
    if (config->protocol == PROTO_VLAN) {
        offset += build_vlan_tag(buffer, config, offset);
    }
    
    /* Add protocol-specific headers */
    switch (config->protocol) {
        case PROTO_IP_UDP:
            payload_size = config->packet_size - offset - sizeof(struct iphdr) - sizeof(struct udphdr);
            if (payload_size < 0) payload_size = 0;
            
            offset += build_ip_header(buffer, config, offset, sizeof(struct udphdr) + payload_size);
            offset += build_udp_header(buffer, config, offset, payload_size);
            break;
            
        case PROTO_IP_TCP:
            payload_size = config->packet_size - offset - sizeof(struct iphdr) - sizeof(struct tcphdr);
            if (payload_size < 0) payload_size = 0;
            
            offset += build_ip_header(buffer, config, offset, sizeof(struct tcphdr) + payload_size);
            offset += build_tcp_header(buffer, config, offset);
            break;
            
        case PROTO_ARP:
            offset += build_arp_packet(buffer, config, offset);
            payload_size = 0;  /* No payload for ARP */
            break;
            
        case PROTO_ETH_RAW:
        case PROTO_VLAN:
        default:
            payload_size = config->packet_size - offset;
            if (payload_size < 0) payload_size = 0;
            break;
    }
    
    /* Add payload if needed */
    if (payload_size > 0) {
        fill_random_payload(buffer, offset, payload_size);
        offset += payload_size;
    }
    
    return offset;  /* Total packet size */
}

/**
 * Calculate delay between packets based on pattern
 */
unsigned long calculate_delay(traffic_config_t *config, unsigned long packet_count) {
    static double phase = 0.0;
    unsigned long delay_usec = 0;
    
    if (config->packets_per_sec == 0) {
        return 1000000;  /* Default to 1 packet per second */
    }
    
    /* Base delay for constant rate */
    unsigned long base_delay = 1000000 / config->packets_per_sec;
    
    /* Apply pattern-specific modifications */
    switch (config->pattern) {
        case TRAFFIC_CONSTANT:
            delay_usec = base_delay;
            break;
            
        case TRAFFIC_BURST:
            /* Send bursts of 10 packets quickly, then pause */
            if (packet_count % 10 == 0) {
                delay_usec = base_delay * 10;
            } else {
                delay_usec = base_delay / 10;
            }
            break;
            
        case TRAFFIC_RANDOM:
            /* Random delay between 0.5x and 1.5x base delay */
            delay_usec = base_delay * (0.5 + (rand() % 1000) / 1000.0);
            break;
            
        case TRAFFIC_RAMP_UP:
            /* Start slow, gradually speed up over time */
            delay_usec = base_delay * (2.0 - (packet_count % 1000) / 1000.0);
            if (delay_usec < base_delay / 2) delay_usec = base_delay / 2;
            break;
            
        case TRAFFIC_RAMP_DOWN:
            /* Start fast, gradually slow down over time */
            delay_usec = base_delay * (0.5 + (packet_count % 1000) / 1000.0);
            if (delay_usec > base_delay * 2) delay_usec = base_delay * 2;
            break;
            
        case TRAFFIC_SINE_WAVE:
            /* Sinusoidal variation */
            phase += 0.01;
            if (phase > 2 * 3.14159) phase = 0;
            delay_usec = base_delay * (1.0 + 0.5 * sin(phase));
            break;
            
        default:
            delay_usec = base_delay;
            break;
    }
    
    return delay_usec > 0 ? delay_usec : 1;  /* Ensure at least 1 microsecond */
}

/**
 * Print usage information
 */
void print_usage(const char *progname) {
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  -i <interface>   : Network interface to use\n");
    printf("  -s <size>        : Packet size in bytes (default: %d)\n", DEFAULT_PACKET_SIZE);
    printf("  -r <rate>        : Packets per second (default: 10)\n");
    printf("  -p <pattern>     : Traffic pattern:\n");
    printf("                      0 = Constant rate (default)\n");
    printf("                      1 = Burst traffic\n");
    printf("                      2 = Random intervals\n");
    printf("                      3 = Ramp up\n");
    printf("                      4 = Ramp down\n");
    printf("                      5 = Sine wave\n");
    printf("  -P <protocol>    : Protocol type:\n");
    printf("                      0 = Raw Ethernet (default)\n");
    printf("                      1 = IP/UDP\n");
    printf("                      2 = IP/TCP\n");
    printf("                      3 = VLAN\n");
    printf("                      4 = ARP\n");
    printf("  -d <MAC>         : Destination MAC address (default: ff:ff:ff:ff:ff:ff)\n");
    printf("  -S <MAC>         : Source MAC address (default: auto from interface)\n");
    printf("  -D <IP>          : Destination IP address (default: 192.168.1.1)\n");
    printf("  -I <IP>          : Source IP address (default: 192.168.1.2)\n");
    printf("  -t <duration>    : Duration in seconds (0 = infinite, default)\n");
    printf("  -v <vlan_id>     : VLAN ID (default: 1)\n");
    printf("  -T <threads>     : Number of generator threads (default: 1)\n");
    printf("  -h               : Show this help message\n");
    exit(1);
}

/**
 * Parse MAC address from string
 */
int parse_mac_address(const char *mac_str, struct ether_addr *mac) {
    unsigned int values[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x", 
               &values[0], &values[1], &values[2], 
               &values[3], &values[4], &values[5]) != 6) {
        return -1;
    }
    
    for (int i = 0; i < 6; i++) {
        if (values[i] > 0xFF) {
            return -1;
        }
        mac->ether_addr_octet[i] = (uint8_t)values[i];
    }
    
    return 0;
}

/**
 * Get MAC address of network interface
 */
int get_interface_mac(const char *ifname, struct ether_addr *mac) {
    struct ifreq ifr;
    int sock;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';
    
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
        perror("ioctl(SIOCGIFHWADDR)");
        close(sock);
        return -1;
    }
    
    memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    close(sock);
    
    return 0;
}

/**
 * Signal handler to catch interrupts
 */
void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nInterrupted, shutting down...\n");
        keep_running = 0;
    }
}

/**
 * Thread function for generating traffic
 */
void *traffic_generator_thread(void *arg) {
    int thread_id = *((int *)arg);
    traffic_config_t config = global_config;
    
    /* Adjust thread-specific values */
    config.src_port += thread_id;
    
    /* Create raw socket */
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock < 0) {
        perror("socket");
        return NULL;
    }
    
    /* Get interface index */
    struct ifreq if_idx;
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_idx.ifr_name, config.interface, IFNAMSIZ-1);
    if (ioctl(sock, SIOCGIFINDEX, &if_idx) < 0) {
        perror("SIOCGIFINDEX");
        close(sock);
        return NULL;
    }
    
    /* Set up socket address */
    struct sockaddr_ll socket_address;
    memset(&socket_address, 0, sizeof(struct sockaddr_ll));
    socket_address.sll_family = AF_PACKET;
    socket_address.sll_ifindex = if_idx.ifr_ifindex;
    socket_address.sll_halen = ETH_ALEN;
    memcpy(socket_address.sll_addr, &config.dst_mac, ETH_ALEN);
    
    /* Prepare packet buffer */
    unsigned char *packet_buffer = (unsigned char *)malloc(MAX_PACKET_SIZE);
    if (!packet_buffer) {
        perror("malloc");
        close(sock);
        return NULL;
    }
    
    /* Generate traffic */
    unsigned long packet_count = 0;
    struct timeval start_time, current_time;
    gettimeofday(&start_time, NULL);
    unsigned long local_packets_sent = 0;
    unsigned long local_bytes_sent = 0;
    
    while (keep_running) {
        /* Check duration if set */
        if (config.duration > 0) {
            gettimeofday(&current_time, NULL);
            unsigned long elapsed = (current_time.tv_sec - start_time.tv_sec);
            if (elapsed >= config.duration) {
                break;
            }
        }
        
        /* Build the packet */
        int packet_size = build_packet(packet_buffer, &config);
        
        /* Send the packet */
        int bytes_sent = sendto(sock, packet_buffer, packet_size, 0,
                              (struct sockaddr*)&socket_address, sizeof(struct sockaddr_ll));
        
        if (bytes_sent < 0) {
            perror("sendto");
            /* Continue despite error */
        } else {
            local_packets_sent++;
            local_bytes_sent += bytes_sent;
            
            /* Update global stats periodically */
            if (local_packets_sent % 1000 == 0) {
                pthread_mutex_lock(&stats_mutex);
                config.packets_sent += local_packets_sent;
                config.bytes_sent += local_bytes_sent;
                pthread_mutex_unlock(&stats_mutex);
                
                local_packets_sent = 0;
                local_bytes_sent = 0;
            }
        }
        
        /* Calculate delay for next packet */
        unsigned long delay_usec = calculate_delay(&config, packet_count);
        usleep(delay_usec);
        
        packet_count++;
    }
    
    /* Update final stats */
    pthread_mutex_lock(&stats_mutex);
    config.packets_sent += local_packets_sent;
    config.bytes_sent += local_bytes_sent;
    pthread_mutex_unlock(&stats_mutex);
    
    /* Clean up */
    free(packet_buffer);
    close(sock);
    
    return NULL;
}

/**
 * Print statistics
 */
void print_statistics(traffic_config_t *config) {
    printf("\nStatistics:\n");
    printf("  Packets sent: %lu\n", config->packets_sent);
    printf("  Bytes sent: %lu\n", config->bytes_sent);
    
    unsigned long duration_sec = config->duration > 0 ? config->duration : 0;
    if (duration_sec > 0) {
        double pps = (double)config->packets_sent / duration_sec;
        double mbps = ((double)config->bytes_sent * 8.0) / (duration_sec * 1000000.0);
        
        printf("  Average rate: %.2f packets/sec (%.2f Mbps)\n", pps, mbps);
    }
}

/**
 * Main entry point
 */
int main(int argc, char *argv[]) {
    int opt;
    traffic_config_t config;
    int i;
    
    /* Set default values */
    memset(&config, 0, sizeof(traffic_config_t));
    strcpy(config.interface, "eth0");
    config.packet_size = DEFAULT_PACKET_SIZE;
    config.packets_per_sec = 10;
    config.pattern = TRAFFIC_CONSTANT;
    config.protocol = PROTO_ETH_RAW;
    config.duration = 0;
    config.num_threads = 1;
    config.vlan_id = 1;
    config.vlan_priority = 0;
    config.src_port = 12345;
    config.dst_port = 80;
    
    /* Set default MAC addresses */
    memset(&config.dst_mac, 0xFF, ETH_ALEN);  /* Broadcast */
    
    /* Set default IP addresses */
    inet_pton(AF_INET, "192.168.1.1", &config.dst_ip);
    inet_pton(AF_INET, "192.168.1.2", &config.src_ip);
    
    /* Parse command line options */
    while ((opt = getopt(argc, argv, "i:s:r:p:P:d:S:D:I:t:v:T:h")) != -1) {
        switch (opt) {
            case 'i':
                strncpy(config.interface, optarg, IFNAMSIZ-1);
                config.interface[IFNAMSIZ-1] = '\0';
                break;
                
            case 's':
                config.packet_size = atoi(optarg);
                if (config.packet_size < MIN_PACKET_SIZE) {
                    config.packet_size = MIN_PACKET_SIZE;
                    fprintf(stderr, "Warning: Packet size increased to minimum (%d bytes)\n", MIN_PACKET_SIZE);
                } else if (config.packet_size > MAX_PACKET_SIZE) {
                    config.packet_size = MAX_PACKET_SIZE;
                    fprintf(stderr, "Warning: Packet size reduced to maximum (%d bytes)\n", MAX_PACKET_SIZE);
                }
                break;
                
            case 'r':
                config.packets_per_sec = atoi(optarg);
                break;
                
            case 'p':
                config.pattern = atoi(optarg);
                if (config.pattern > TRAFFIC_SINE_WAVE) {
                    fprintf(stderr, "Invalid pattern, using default\n");
                    config.pattern = TRAFFIC_CONSTANT;
                }
                break;
                
            case 'P':
                config.protocol = atoi(optarg);
                if (config.protocol > PROTO_ARP) {
                    fprintf(stderr, "Invalid protocol, using default\n");
                    config.protocol = PROTO_ETH_RAW;
                }
                break;
                
            case 'd':
                if (parse_mac_address(optarg, &config.dst_mac) != 0) {
                    fprintf(stderr, "Invalid destination MAC address\n");
                    print_usage(argv[0]);
                }
                break;
                
            case 'S':
                if (parse_mac_address(optarg, &config.src_mac) != 0) {
                    fprintf(stderr, "Invalid source MAC address\n");
                    print_usage(argv[0]);
                }
                break;
                
            case 'D':
                if (inet_pton(AF_INET, optarg, &config.dst_ip) != 1) {
                    fprintf(stderr, "Invalid destination IP address\n");
                    print_usage(argv[0]);
                }
                break;
                
            case 'I':
                if (inet_pton(AF_INET, optarg, &config.src_ip) != 1) {
                    fprintf(stderr, "Invalid source IP address\n");
                    print_usage(argv[0]);
                }
                break;
                
            case 't':
                config.duration = atoi(optarg);
                break;
                
            case 'v':
                config.vlan_id = atoi(optarg);
                if (config.vlan_id > 4095) {
                    fprintf(stderr, "Invalid VLAN ID, using default\n");
                    config.vlan_id = 1;
                }
                break;
                
            case 'T':
                config.num_threads = atoi(optarg);
                if (config.num_threads < 1) {
                    config.num_threads = 1;
                } else if (config.num_threads > MAX_THREADS) {
                    config.num_threads = MAX_THREADS;
                    fprintf(stderr, "Warning: Number of threads reduced to maximum (%d)\n", MAX_THREADS);
                }
                break;
                
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    /* Get the MAC address of the interface if not specified */
    if (memcmp(&config.src_mac, "\0\0\0\0\0\0", ETH_ALEN) == 0) {
        if (get_interface_mac(config.interface, &config.src_mac) != 0) {
            fprintf(stderr, "Error getting MAC address of interface %s\n", config.interface);
            return EXIT_FAILURE;
        }
    }

    /* Set up signal handler for graceful termination */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Save config to global variable for thread access */
    memcpy(&global_config, &config, sizeof(traffic_config_t));

    printf("Traffic generator starting on interface %s\n", config.interface);
    printf("Protocol: %d, Pattern: %d, Rate: %lu pps, Size: %u bytes\n", 
           config.protocol, config.pattern, config.packets_per_sec, config.packet_size);
    printf("Duration: %u seconds (0 = infinite)\n", config.duration);
    printf("Number of threads: %u\n", config.num_threads);
    
    /* Print MAC and IP addresses */
    char src_mac_str[18], dst_mac_str[18];
    char src_ip_str[INET_ADDRSTRLEN], dst_ip_str[INET_ADDRSTRLEN];
    
    sprintf(src_mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
            config.src_mac.ether_addr_octet[0], config.src_mac.ether_addr_octet[1],
            config.src_mac.ether_addr_octet[2], config.src_mac.ether_addr_octet[3],
            config.src_mac.ether_addr_octet[4], config.src_mac.ether_addr_octet[5]);
    
    sprintf(dst_mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
            config.dst_mac.ether_addr_octet[0], config.dst_mac.ether_addr_octet[1],
            config.dst_mac.ether_addr_octet[2], config.dst_mac.ether_addr_octet[3],
            config.dst_mac.ether_addr_octet[4], config.dst_mac.ether_addr_octet[5]);
    
    inet_ntop(AF_INET, &config.src_ip, src_ip_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &config.dst_ip, dst_ip_str, INET_ADDRSTRLEN);
    
    printf("Source MAC: %s  Destination MAC: %s\n", src_mac_str, dst_mac_str);
    printf("Source IP: %s  Destination IP: %s\n", src_ip_str, dst_ip_str);
    
    if (config.protocol == PROTO_IP_UDP || config.protocol == PROTO_IP_TCP) {
        printf("Source Port: %u  Destination Port: %u\n", config.src_port, config.dst_port);
    }
    
    if (config.protocol == PROTO_VLAN) {
        printf("VLAN ID: %u  Priority: %u\n", config.vlan_id, config.vlan_priority);
    }
    
    /* Create traffic generator threads */
    pthread_t threads[MAX_THREADS];
    for (i = 0; i < config.num_threads; i++) {
        int *thread_id = malloc(sizeof(int));
        if (thread_id == NULL) {
            fprintf(stderr, "Error allocating memory for thread ID\n");
            return EXIT_FAILURE;
        }
        *thread_id = i;
        
        if (pthread_create(&threads[i], NULL, traffic_generator_thread, thread_id) != 0) {
            fprintf(stderr, "Error creating thread %d\n", i);
            free(thread_id);
            /* Terminate existing threads */
            keep_running = 0;
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            return EXIT_FAILURE;
        }
    }
    
    /* Set up timer for duration if specified */
    if (config.duration > 0) {
        printf("Running for %u seconds...\n", config.duration);
        sleep(config.duration);
        printf("Duration complete, stopping traffic...\n");
        keep_running = 0;
    } else {
        printf("Running indefinitely. Press Ctrl+C to stop.\n");
    }
    
    /* Wait for all threads to complete */
    for (i = 0; i < config.num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* Print final statistics */
    print_statistics(&global_config);
    
    printf("Traffic generator stopped.\n");
    return EXIT_SUCCESS;
}
