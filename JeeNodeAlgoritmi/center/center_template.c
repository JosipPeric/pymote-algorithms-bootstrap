#define DEBUG_SERIAL 1
#include <avr/eeprom.h>
#include <JeeLib.h>
#include <LocLib.h>

/*
 * Node's permanent configuration settings for this program is set in EEPROM
 * with following layout:
 */
#define ID_EEPROM_ADDR (uint8_t*)0x020
#define GROUP_EEPROM_ADDR (uint8_t*)0x021

/* New settings in line with those in rf12Config:
 * byte  0x020  NodeId 1 - 16
 * byte  0x021  GroupId
 */

#define PROTOCOL_EEPROM_ADDR (uint8_t*)0x320
/*
 * byte  0x320  RESEND_DELAY_BASE         Protocol param, communications.cpp
 * byte  0x321  RESEND_COUNT_BASE           "
 */

#define OUTBOX_QUEUE_SIZE 10
#define MAX_NODES 16

// Node statuses
enum {
  AVAILABLE, ACTIVE, PROCESSING, SATURATED, CENTER, DONE
};

// Message types
enum {
  Reset,
  Sync,
  SetStatus,
  SetNeighbors,
  Wakeup,
  M,
  Resolve,
};

struct setstatusS {
  uint8_t status;
};

struct setneighborsS {
  uint8_t neighbors[MAX_NODES];
};

struct MS {
  uint8_t val;
};

struct resolveS {
  uint8_t val;
};

struct Message {
  uint8_t header;
  uint8_t source;
  union {
    struct setstatusS setstatus;
    struct setneighborsS setneighbors;
    struct MS M;
    struct resolveS resolve;
  };
};

Message g_message;

struct OutboxQueue {
  Message messages[OUTBOX_QUEUE_SIZE];
  int destinations[OUTBOX_QUEUE_SIZE];
  int headers[OUTBOX_QUEUE_SIZE];
  int lengths[OUTBOX_QUEUE_SIZE];
  uint8_t head;
  uint8_t tail;
};

OutboxQueue outbox_queue = {
  .messages={},
  .destinations={},
  .headers={},
  .lengths={},
  .head=0,
  .tail=0
};

MilliTimer processOutboxTimer;

// jeenode ports where ledstate is plugged
LedStatePlug ledstate(2, 3);

DisAlgProtocol rf(eeprom_read_byte(ID_EEPROM_ADDR) & 0x1F,
                  eeprom_read_byte(PROTOCOL_EEPROM_ADDR),
                  eeprom_read_byte(PROTOCOL_EEPROM_ADDR+1));

void setup();
void loop();
void reset();
void set_status(uint8_t);
//uint8_t update_maxval(uint8_t);
uint8_t pushOutboxQueue(uint8_t, uint8_t, void*, uint8_t);
void processOutbox();
uint8_t count_neighbors();
uint8_t count_max_neighbors();
void remove_neighbor(uint8_t);
uint8_t find_neighbor();
uint8_t find_max_neighbor();
void resolve_function();
void center_finding();

// register containing value
uint8_t g_distance = 0;
uint8_t g_maxdistance = 0;
uint8_t g_neighbors[MAX_NODES];
uint8_t g_destnodes[MAX_NODES]; //neighbors which are to be deleted upon receiving header M 
uint8_t g_max_neighbors[MAX_NODES];
void setup() {
  g_id = eeprom_read_byte(ID_EEPROM_ADDR) & 0x1F;
  g_group = eeprom_read_byte(GROUP_EEPROM_ADDR);

#ifdef DEBUG_SERIAL
  Serial.begin(57600);
  // set timeout for parseInt command
  Serial.setTimeout(10000);
  DS("[center]");
  DS("Node id: ");
  D(g_id);
  DS("Node group: ");
  D(g_group);
#endif // DEBUG_SERIAL

  reset();
  rf12_initialize(g_id, RF12_868MHZ, g_group);
}

void reset() {
  // initialization light show
  for (int i = 15; i >= 0; i--) {
    set_status(i);
    g_max_neighbors[i] = 0;
    delay(100);
  }
  outbox_queue.head = outbox_queue.tail = 0;
  memcpy(g_destnodes, g_neighbors, MAX_NODES);
  g_maxdistance = 0;
  g_distance = 0;
  set_status(AVAILABLE);
}

// MAIN FUNCTION
/*
 * Listen incoming messages via rf on node and act and respond
 * according to their header, that is, first three bits of rf12_data.
 */
void loop() {
  // ************************* Messages *************************
  if (rf.receive(&g_message) && (are_neighbors(g_id, g_message.source) || g_message.source==31)) {
    DS("Message received: ");
    switch (g_message.header) {
      case Reset:
        DS("Reset");
        reset();
        break;
      case Sync:
        DS("Sync");
        DS("Eccentricity = \n");
        D(g_maxdistance);
        processOutboxTimer.set(g_id*20);
        break;
      case SetStatus:
        DS("SetStatus status=");
        D(g_message.setstatus.status);
        set_status(g_message.setstatus.status);
        break;
      case SetNeighbors:
        DS("SetNeighbors neighbors=");
        for (int i = 0; i < MAX_NODES; ++i) {
          D(g_message.setneighbors.neighbors[i]);
        }
        memcpy(g_neighbors, g_message.setneighbors.neighbors, MAX_NODES);
        memcpy(g_destnodes, g_message.setneighbors.neighbors, MAX_NODES);
        for (int i = 0; i < MAX_NODES; ++i) {
        }
        break;
      case Wakeup:
        switch (g_status) {
            case AVAILABLE:
              DS("Wakeup");
              pushOutboxQueue(Wakeup, 0, &g_message, sizeof(MS));
              if (count_neighbors() > 1){
                set_status(ACTIVE);  
              } else {
                set_status(PROCESSING); //In case the node is a leaf (has only 1 neighbor)
                g_message.M.val = g_maxdistance + 1; //Is gonna be 1
                pushOutboxQueue(M, 0, &g_message, sizeof(MS));
              }
              break;
            default:
              break;
        }
        break;
      case M:
        // to send use pushOutboxQueue(header, destination, &g_message, length);
        DS("M");
        switch (g_status) {
          case AVAILABLE:
          case ACTIVE:
            if (g_message.M.val > g_maxdistance) {
              g_maxdistance = g_message.M.val;
              g_max_neighbors[g_message.source] = g_maxdistance;
            }
            if(count_neighbors() >= 2)
            {
              remove_neighbor(g_message.source);
            }
            if (count_neighbors() == 1){
              g_message.M.val = g_maxdistance + 1;
              pushOutboxQueue(M, find_neighbor(), &g_message, sizeof(MS));
              set_status(PROCESSING);
            }
          break;
          case PROCESSING:
            DS("Saturated");
            set_status(SATURATED);
            g_distance = g_message.M.val;
            center_finding();
            if (g_distance > g_maxdistance) {
              g_maxdistance = g_distance;
            }
            resolve_function();
          break;
        }
        break;
      case Resolve:
        DS("Resolve");
        switch (g_status) {
          case PROCESSING:
            g_distance = g_message.resolve.val;
            center_finding();
            if (g_distance > g_maxdistance) {
              g_maxdistance = g_distance;
            }
            resolve_function();
          break;
        }
        break;
      default:
        DS("Header not recognized");
        break;
    }
  }

  // ********************** Spontaneously ***********************
 /* if (ledstate.buttonCheck()==BlinkPlug::OFF1) {
    g_message.maxval.val = g_maxval;
    rf.send(MaxVal, 0, &g_message, sizeof(maxvalS));
    set_status(MAX);
  }

*/
  // ********************** Timers/Alarms ***********************
  if (processOutboxTimer.poll()) {
    processOutboxQueue();
  }
}

// PROCEDURES

uint8_t pushOutboxQueue(uint8_t header, uint8_t destination, void *message, uint8_t length) {
  if ((outbox_queue.tail+1)%OUTBOX_QUEUE_SIZE==outbox_queue.head) {
    DS("Outbox queue full.");
    return 0;
  }
  outbox_queue.destinations[outbox_queue.tail] = destination;
  outbox_queue.headers[outbox_queue.tail] = header;
  outbox_queue.lengths[outbox_queue.tail] = length;
  memcpy(&outbox_queue.messages[outbox_queue.tail], message, sizeof(g_message));
  outbox_queue.tail = (outbox_queue.tail+1)%OUTBOX_QUEUE_SIZE;
  return 1;
}

// If there are messagess in the outbox send first one
void processOutboxQueue() {
  if (outbox_queue.head!=outbox_queue.tail) {
    rf.send(outbox_queue.headers[outbox_queue.head], outbox_queue.destinations[outbox_queue.head],
         &outbox_queue.messages[outbox_queue.head], outbox_queue.lengths[outbox_queue.head]);
    outbox_queue.head = (outbox_queue.head+1)%OUTBOX_QUEUE_SIZE;
  }
}


void set_status(uint8_t s) {
  g_status = s;
  ledstate.set(s);
}

uint8_t are_neighbors(uint8_t id1, uint8_t id2) {
  uint8_t ret = 0;
  for (int i = 0; i < MAX_NODES; ++i) {
    ret |= g_neighbors[i] && id2==g_neighbors[i];
    //D(id2);
    //D(g_neighbors[i]);
  }
  //DS("ret");
  //D(ret);
  return ret;
}

// Returns the number of neighbors of a node
uint8_t count_neighbors(){  
  uint8_t cnt = 0;
  for (int i=0; i < MAX_NODES; i++) {  //Check if the node has more than 1 neighbor
          if (g_destnodes[i] > 0){ // If a node has a neighbor, the value is its ID which is > 0 (1-16)
            cnt += 1;
          }
  }
  return cnt;
}

// Checks how many nodes have sent the highest value (e.g. with 3 neighbors - if two neighbors send "5" and one sends "4", function returns 2)
uint8_t count_max_neighbors(){  
  uint8_t cnt = 0;
  uint8_t max_dist = 0;
  for (int i=0; i < MAX_NODES; i++) { 
          if ((g_max_neighbors[i] > 0) && (g_max_neighbors[i] > max_dist)){ // If a node has a neighbor, the value is its ID which is > 0 (1-16)
            max_dist = g_max_neighbors[i];
          }
  }

  for (int i=0; i < MAX_NODES; i++) {  //Check if the node has more than 1 neighbor
          if (g_max_neighbors[i] == max_dist){ // If a node has a neighbor, the value is its ID which is > 0 (1-16)
            cnt += 1;
          }
  }

  return cnt;
}

// Removes a specified node from g_destnodes
void remove_neighbor(uint8_t id){
  for (int i = 0; i < MAX_NODES; i++){
    if (g_destnodes[i] == id){
      g_destnodes[i] = 0;
    }
  }
}

// Finds the only remaining neighbor of a node
uint8_t find_neighbor(){;
  for (int i = 0; i < MAX_NODES; i++){
    if (g_destnodes[i] > 0){
      return g_destnodes[i];
      break;
    }
  }
}

// Finds the neighbor that has sent the highest value
uint8_t find_max_neighbor(){
  uint8_t max_val = 0;
  uint8_t max_id = 0; 
  for (int i = 0; i < MAX_NODES; i++){
    if ((g_max_neighbors[i] > 0) && (g_max_neighbors[i] > max_val)){
      max_id = i;
      max_val = g_max_neighbors[i];
    }
  }
  return max_id;
}

// Sends eccentricity through the network
void resolve_function(){
  
  uint8_t max_neighbor = find_max_neighbor();

  if (count_max_neighbors() > 1){
    g_message.resolve.val = g_maxdistance + 1; 
    pushOutboxQueue(Resolve, 0, &g_message, sizeof(resolveS));
  } else{
    g_message.resolve.val = g_distance + 1;
    pushOutboxQueue(Resolve, max_neighbor, &g_message, sizeof(resolveS));
    g_message.resolve.val = g_maxdistance + 1;
    for (int i = 0; i < MAX_NODES; ++i){
      if ((g_neighbors[i] > 0) && (g_neighbors[i] != max_neighbor)){
        pushOutboxQueue(Resolve, g_neighbors[i], &g_message, sizeof(resolveS));
      }
    }
  } 
}

// Checks if node is center
void center_finding(){
  // Fill in
}