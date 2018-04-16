from pymote.algorithm import NodeAlgorithm
from pymote.message import Message
from pymote.algorithm.sensor_new import *

class TemperatureSensor(Sensor):

    @node_in_network
    def read(self, node):
        temp = randint(1, 255)
        return {'Temp': temp }

class Temp_Flood(NodeAlgorithm):
    required_params = ('informationKey',)
    default_params = {'neighborsKey':'Neighbors', 'temperatureKey': 'Temperature', 'maxtempKey': 'Maxtemp'}

    def initializer(self):
        ini_nodes = []
        for node in self.network.nodes():
            node.memory[self.neighborsKey] = node.compositeSensor.read()['Neighbors']
            node.status = 'IDLE'
            if node.memory.has_key(self.informationKey):
                node.status = 'INITIATOR'
                ini_nodes.append(node)
        for ini_node in ini_nodes:
            self.network.outbox.insert(0,Message(header=NodeAlgorithm.INI,
                                                 destination=ini_node))

    def initiator(self, node, message):
        if message.header==NodeAlgorithm.INI:
            print "Message:" , node.memory[self.informationKey] , " Temp:" , node.memory['Temperature']
            node.memory['Maxtemp'] = node.memory['Temperature']
            #Prakticki nepotrebno jer smo dole odredili za prvi korak initiator node I = Temp
            if node.memory[self.informationKey] > node.memory['Temperature']:
                node.send(Message(header='Information',  # default destination: send to every neighbor
                                  data=node.memory[self.informationKey]))
            else:
                node.send(Message(header='Information',  # default destination: send to every neighbor
                                  data=node.memory['Temperature']))
            node.status = 'DONE'

    def idle(self, node, message):
        if message.header=='Information':
            print "Message:" , message.data ," Temp:" , node.memory['Temperature']
            node.memory['Maxtemp'] = message.data
            node.memory[self.informationKey] = message.data
            destination_nodes = list(node.memory[self.neighborsKey])
            destination_nodes.remove(message.source) # send to every neighbor-sender
            if destination_nodes:
                #Je li dobivena tj. primljena  poruka (temp) veca od temperature ovog cvora?
                if node.memory[self.informationKey] > node.memory['Temperature']:
                    node.send(Message(destination=destination_nodes, header='Information', 
                                      data=message.data))
                    node.memory['Maxtemp'] = message.data
                else:
                    node.send(Message(destination=destination_nodes, header='Information', 
                                      data=node.memory['Temperature']))
                    node.memory['Maxtemp'] = node.memory['Temperature']
        node.status = 'DONE'

    def done(self, node, message):
        if message.data > node.memory['Maxtemp']:
            node.memory[self.informationKey] = message.data
            destination_nodes = list(node.memory[self.neighborsKey])
            destination_nodes.remove(message.source) # send to every neighbor-sender
            node.memory['Maxtemp'] = message.data
            if destination_nodes:
                node.send(Message(destination=destination_nodes, header='Information', 
                                      data=message.data))

    STATUS = {
              'INITIATOR': initiator,
              'IDLE': idle,
              'DONE': done,
             }


