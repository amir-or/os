# OS 24 EX1
import pandas as pd
import matplotlib.pyplot as plt

data = pd.read_csv(r"C:\path\to\output.csv")
data = data.to_numpy()

KiB = 2**10
MiB=2**20

l1_size = 192*KiB / 6
l2_size = 1.5*MiB / 6
l3_size = 9 *MiB




plt.plot(data[:, 0], data[:, 1], label="Random access")
plt.plot(data[:, 0], data[:, 2], label="Sequential access")
plt.xscale('log')
plt.yscale('log')
plt.axvline(x=l1_size, label="L1 (?? ?iB)", c='r')
plt.axvline(x=l2_size, label="L2 (?? ?iB)", c='g')
plt.axvline(x=l3_size, label="L3 (?? ?iB)", c='brown')
plt.legend()
plt.title("Latency as a function of array size")
plt.ylabel("Latency (ns log scale)")
plt.xlabel("Bytes allocated (log scale)")
plt.show()
