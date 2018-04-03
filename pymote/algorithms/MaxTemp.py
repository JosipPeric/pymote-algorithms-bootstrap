
# coding: utf-8

# In[86]:


from pymote import *
from pymote.sensor import TemperatureSensor
from pymote.sensor import NeighborsSensor
node = Node()


# In[87]:


net = Network()
net.add_node(node)
node = net.add_node()


# In[88]:


node.compositeSensor = (TemperatureSensor, 'Temp', NeighborsSensor, 'Neighbor' )
#node.compositeSensor.read()


# In[89]:


get_ipython().magic(u'matplotlib inline')


# In[90]:


for node in net.nodes():
    node.commRange = 600
net.recalculate_edges()


# In[91]:


net_gen = NetworkGenerator(10) 
net = net_gen.generate_random_network()
net.show()


# In[92]:


from pymote.algorithms.broadcast import Flood
from pymote.message import Message
message = Message()
net.communicate()


# In[103]:


from pymote.algorithm import NodeAlgorithm
from pymote.message import Message



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
            #Praktički nepotrebno jer smo dole odredili za prvi korak initiator node I = Temp
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
                #Je li dobivena tj. primljena  poruka (temp) veća od temperature ovog čvora?
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


# In[104]:


net.algorithms = ( (Temp_Flood, {'informationKey':'I'}), )


# In[105]:


for node in net.nodes():
    node.compositeSensor = (TemperatureSensor, 'Temp', NeighborsSensor, 'Neighbor' )
    node.memory['Temperature'] = node.compositeSensor.read()['Temp']
    print node.memory['Temperature']


# In[106]:


some_node = net.nodes()[0]
some_node.memory['I'] = some_node.memory['Temperature']
print some_node.memory


# In[107]:


sim = Simulation(net)
sim.run()


# In[108]:


for node in net.nodes():
    print node.memory['Maxtemp']

