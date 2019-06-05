#define NO_NODE 0
#define NO_SEQN 65535
#define NODES 26

#ifdef DEBUG
# define LOG(args...) printf(args)
#else
# define LOG(...)
#endif

typedef struct __attribute__((__packed__)) value_struct{
  float x_value;
  float y_value;
}value_struct;

typedef struct {
} 
__attribute__((packed))
app_s_payload;

typedef struct {
  crystal_addr_t src;
  uint16_t seqn;
  //uint8_t payload[CRYSTAL_PAYLOAD_LENGTH];
  struct value_struct payload;
  uint8_t id_buffer[ID_BUFFER_SIZE];
} 
__attribute__((packed))
app_t_payload;

typedef struct {
  crystal_addr_t src;
  uint16_t seqn;
  struct value_struct update;
  uint8_t id_buffer[ID_BUFFER_SIZE];
} 
__attribute__((packed))
app_a_payload;

#ifdef FLOCKLAB
//have all the nodes you are going to use consecutive here
int id_mapping[26] = {1,2,4,8,15,33,3,32,31,28,6,16,22,10,18,27,24,23,20,19,17,13,25,11,14,7};
#endif

