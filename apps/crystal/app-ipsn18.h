#define NO_NODE 0
#define NO_SEQN 65535

typedef struct {
} 
__attribute__((packed))
app_s_payload;

typedef struct {
  crystal_addr_t src;
  uint16_t seqn;
  uint8_t payload[CRYSTAL_PAYLOAD_LENGTH];
} 
__attribute__((packed))
app_t_payload;

typedef struct {
  crystal_addr_t src;
  uint16_t seqn;
} 
__attribute__((packed))
app_a_payload;
