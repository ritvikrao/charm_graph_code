from matplotlib import animation
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
#%matplotlib qt

import sys
import math
import re

def msdigits(num, k):
    x = math.log10(num)
    digits = int(1+ num/(pow(10, (int(x) -1)))) * pow(10, (int(x) -1))
    return digits

def animate(i):
    for x in axes.containers:
        x.remove()
    bars = lsts[i]
    plt.bar([j for j in range(len(bars))], bars, color=(0.1,0.1,0.9))

lsts = []
with open(sys.argv[1], 'r') as f:
    while True:
        line1 = f.readline()
        if not line1:
            break
        line2 = re.sub('[^0-9 \n]', '', line1).rstrip()
        line = line2.split(' ')
        lsts.append([float(x) for x in line  ] )

y = 0
for i in range(len(lsts)):
    y = max( y, max(lsts[i]))
print(y, msdigits(y,2))

fig = plt.figure(figsize=(8,6))
axes = fig.add_subplot(1,1,1)
axes.set_ylim(0, msdigits(y,2))
plt.title("histogram of unprocessed updates")

anim = FuncAnimation(fig, animate, len(lsts), interval = 100, repeat = False)
anim.save(filename="s.mp4",writer="ffmpeg", fps=10)
plt.show()
