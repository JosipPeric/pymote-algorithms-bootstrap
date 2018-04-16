from pymote.algorithm import NodeAlgorithm
from pymote.message import Message
from pymote.sensor import *
import random

class TemperatureSensor(Sensor):

    @node_in_network
    def read(self, node):
        temp = randint(1, 255)
        return {'Temp': temp }

class saturacija(NodeAlgorithm):
    #required_params = ('informationKey',)
    default_params = {'neighborsKey':'Neighbors', 'temperatureKey': 'Temperature', 'maxtempKey': 'Maxtemp'}

    def initializer(self):
        ini_nodes = []
        for i in range (1):
            self.randomini = random.randint(0,len(self.network.nodes())-1)
            ini_nodes.append(self.network.nodes()[self.randomini])
        for node in self.network.nodes():
            node.memory[self.neighborsKey] = node.compositeSensor.read()['Neighbors']
            node.memory['Susjedi'] = node.compositeSensor.read()['Neighbors']
            node.memory['Maxtemp'] = node.compositeSensor.read()['Temp']
            node.memory['Ini'] = 0
            node.status = 'AVAILABLE'
            #if node.memory.has_key(self.informationKey):
            #    node.status = 'INITIATOR'
            #    ini_nodes.append(node)
        print (ini_nodes)
        for ini_node in ini_nodes:
            ini_node.memory['Ini'] = 1
            self.network.outbox.insert(0,Message(header=NodeAlgorithm.INI,
                                                 destination=ini_node))

    def available(self, node, message):
        # OVO JE ZA PRVOG (INIT)
        if message.header==NodeAlgorithm.INI:
            node.send(Message(header='Wakeup'))
            if (len(node.memory['Susjedi']) == 1): # Poseban slucaj kad init ima samo 1 susjeda? (Graf ima 2 cvora)
                node.send(Message(header='M', data=node.memory['Maxtemp'], destination=node.memory['Susjedi']))
                node.status = 'PROCESSING'
            else:
                node.status = 'ACTIVE'
        # OVO JE ZA SVE OSTALE CVOROVE
        if message.header=='Wakeup':
            destination_nodes = list(node.memory['Susjedi'])
            destination_nodes.remove(message.source) # Pobrisali smo sendera iz liste primatelja
            node.send(Message(header='Wakeup', destination=destination_nodes))
            if (len(node.memory['Susjedi']) == 1): 
                node.send(Message(header='M', data=node.memory['Maxtemp'], destination=node.memory['Susjedi']))
                node.status = 'PROCESSING'
            else:
                node.status = 'ACTIVE'

    def active(self, node, message):
        if message.header=='M':
            if message.data > node.memory['Maxtemp']:
                node.memory['Maxtemp'] = message.data
            if (message.source in node.memory['Susjedi']):
                (node.memory['Susjedi']).remove(message.source)
            if (node.memory['Ini'] == 1):
                if (len(node.memory['Susjedi']) == 0):
                    node.send(Message(header='Flood', data=node.memory['Maxtemp'], destination=node.memory['Neighbors']))
                    node.status = 'SATURATED'
            else:
                if (len(node.memory['Susjedi']) == 1):
                    node.send(Message(header='M', data=node.memory['Maxtemp'], destination=node.memory['Susjedi']))
                    node.status = 'PROCESSING'



        if message.header=='Wakeup':
            if (message.source in node.memory['Susjedi']):
                (node.memory['Susjedi']).remove(message.source)
            node.send(Message(header='Backedge', destination=message.source))
            if (len(node.memory['Susjedi']) == 1): 
                node.send(Message(header='M', data=node.memory['Maxtemp'], destination=node.memory['Susjedi']))
                node.status = 'PROCESSING'

        if message.header=='Backedge':
            if (message.source in node.memory['Susjedi']):
                (node.memory['Susjedi']).remove(message.source)
            if (len(node.memory['Susjedi']) == 1): 
                node.send(Message(header='M', data=node.memory['Maxtemp'], destination=node.memory['Susjedi']))
                node.status = 'PROCESSING'

    def processing(self, node, message):
        if message.header=='Flood':
            node.memory['Maxtemp'] = message.data
            node.send(Message(header='Flood', data=node.memory['Maxtemp'], destination=node.memory['Neighbors']))
            node.status = 'SATURATED'
            


    def saturated(self, node, message):
        pass

    STATUS = {
              'AVAILABLE' : available,
              'ACTIVE': active,
              'PROCESSING': processing,
              'SATURATED': saturated,
             }
