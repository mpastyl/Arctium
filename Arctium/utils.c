
int get_int(float x)
{
  return ((int) x);
}

int get_dec(float x)
{
  int32_t a = (int32_t) x;
  int32_t b;
  if (a>=0)
    b = ((int32_t) (10000.0*x)) - a*10000;
  else
    b = ((int32_t) (-10000.0*x)) + (a*10000) ;
  return (b);
}

int find_id(){
  int temp_id = 0;
  #ifdef FLOCKLAB
  //find your new node_id
  int i;
  for(i=0;i<NODES;i++){
    if (node_id == id_mapping[i]) break;
  }
  if (i<NODES) {
    printf("My new name is %d \n",i+1);
    temp_id = i+1;
  }
  else{
    printf("ERROR: Couldn't find my new name %d, exiting\n",i);
    return 1;
  }
  #else
  temp_id = node_id;
  printf("My new id is %d \n",temp_id);
  #endif
   
  return temp_id;

}

int is_full(uint8_t * buffer, int size){
  return buffer[size-1]; // If the last id is 0 then we know there is space
}
int is_empty(uint8_t * buffer, int size){
  return (buffer[0] == 0); //If the first is 0, we know that the rest should also be 0
}

int get_first_empty(uint8_t * buffer, int size){
  int i;
  for(i=0; i<size; i++){
    if (buffer[i] == 0) 
      return i;
  }
  return size;
}

int get_size(uint8_t * buffer, int size){
  return get_first_empty(buffer,size);
}

int is_member(uint8_t * buffer, int size, uint8_t value){
  int i;
  for(i=0; i<size; i++){
    if (buffer[i] == value)
      return 1;
  }
  return 0;
}

void add_to_buffer(uint8_t * buffer, int size, uint8_t value){
  int i;
  for(i=0; i<size; i++){
    if (buffer[i] == 0){
      buffer[i] = value;
      break;
    }
  }
}

//void add_to_value_buffer(struct value_struct * buffer, uint8_t * id_buffer, int size, struct value_struct value){
//  int i = get_first_empty(id_buffer, size);
//  buffer[i].x_value = value.x_value;
//  buffer[i].y_value = value.y_value;
//}

void add_to_both_buffers( uint8_t * id_buffer, struct value_struct * value_buffer, int size, uint8_t id, struct value_struct value){
  add_to_buffer(id_buffer, size, id);
  int i = get_first_empty(id_buffer, size);
  value_buffer[i-1].x_value = value.x_value; //safe to -1 because i just added so i>0
  value_buffer[i-1].y_value = value.y_value;
}

void copy_buffer(uint8_t * from, uint8_t * to, int size){
  int i;
  for(i=0; i<size; i++){
    to[i] = from[i];
  }
}

void flush_buffer(uint8_t * buffer, int size){
  int i;
  for(i=0; i<size; i++){
    buffer[i] = 0;
  }
}

void flush_value_buffer(struct value_struct * buffer, int size){
  int i;
  for(i=0; i<size; i++){
    buffer[i].x_value = 0;
    buffer[i].y_value = 0;
  }
}

void remove_from_buffers(uint8_t * buffer, struct value_struct * val_buffer, int size, uint8_t value){
  int i;
  //When you find the value to be removed, shift the rest one position before
  int found = 0;
  for(i=0; i<(size-1); i++){
    if (buffer[i] == value){
      found = 1;
    }
    buffer[i] = buffer[i + found];
    val_buffer[i] = val_buffer[i + found];
  }
  buffer[size-1] = 0;
  val_buffer[size-1].x_value = 0;
  val_buffer[size-1].y_value = 0;
}

void find_aggregate (struct value_struct * buffer, int size, struct value_struct * sum){
    sum->x_value = 0;
    sum->y_value = 0;
    int i;
    for(i=0; i<size; i++){
      sum->x_value += buffer[i].x_value;
      sum->y_value += buffer[i].y_value;
    }
}
