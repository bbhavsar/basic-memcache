import bmemcached
import time
import random

client = bmemcached.Client(('127.0.0.1:11211', ))

print client.set('k1', 'hello')
print client.get('k1')

a=[1, 2, 3]
print client.set('k', a)
print client.get('k')

time.sleep(random.randint(1, 5))

b = 102400
print client.set('k2', b)
print client.get('k2')

f=[1.111, 2.222, 3.333]
print client.set('k3', f)
print client.get('k3')


'''
print client.set('key1', 'val1')
print client.set('key2', 'val2')
print client.set('key3', 'val3')
print client.get('key1')
print client.get('key2')
print client.get('key3')
print client.set('key1', 'overwrittenval1')
print client.get('key1')
'''



