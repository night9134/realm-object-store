import random

for _ in xrange(100000):
    print random.randint(0, 60000)
print
print

for _ in xrange(100000):
    a = random.choice(['a', 'd', 'm'])
    if a == 'a':
        print "a {}".format(random.randint(0, 60000))
    elif a == 'm':
        print "m {} {}".format(random.randint(0, 100000), random.randint(0, 60000))
    else:
        print "d {}".format(random.randint(0, 100000))
