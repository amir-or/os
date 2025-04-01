import numpy as np
import matplotlib.pyplot as plt

# Constants
KiB = 2**10
MiB = 2**20

# Cache sizes in bytes
l1_size = 192 * KiB // 6
l2_size = int(1.5 * MiB) // 6
l3_size = 9 * MiB

# Load data
data = np.loadtxt('output.csv', delimiter=',')

# Plot latency curves
plt.plot(data[:, 0], data[:, 1], label="Random access")
plt.plot(data[:, 0], data[:, 2], label="Sequential access")

# Axes scales
plt.xscale('log')
plt.yscale('log')

# Vertical lines for cache levels
plt.axvline(x=l1_size, label=f"L1 ({l1_size // KiB} KiB)", color='red')
plt.axvline(x=l2_size, label=f"L2 ({l2_size // KiB} KiB)", color='green')
plt.axvline(x=l3_size, label=f"L3 ({l3_size // MiB} MiB)", color='brown')

# Labels and title
plt.xlabel("Bytes allocated (log scale)")
plt.ylabel("Latency (ns, log scale)")
plt.title("Latency vs Array Size – <Your CPU Model Here>")
plt.legend()
plt.grid(True, which="both", ls="--", linewidth=0.5)
plt.tight_layout()

# Count negative values in each column
neg_random = np.sum(data[:, 1] < 0)
neg_sequential = np.sum(data[:, 2] < 0)

print(f"Negative values in Random Access column: {neg_random}")
print(f"Negative values in Sequential Access column: {neg_sequential}")

# Show plot
plt.show()





