# Arctium

Arctium is an application-protocol codesign designed for continious threshold monitoring on top of synchronous transmission using in-network aggregation. 

It uses Geometric Monitoring as an application and Crystal as the underlying protocol.
In this repo, a simple example where the variance of all sensor readings is monitored using Geometric Monitoring, under the Arctium folder.

More information about the design can be found in  following paper.
 * **Continuous Monitoring meets Synchronous Transmissions and In-Network Aggregation**, Charalampos Stylianopoulos, Magnus Almgren, Olaf Landsiedel, Marina Papatriantafilou. In Proceedings of the 15th International Conference on Distributed Computing in Sensor Systems (DCOSS) 2019.

It heavily relies on the Crystal protocol:

 * **Interference-Resilient Ultra-Low Power Aperiodic Data Collection**, Timofei Istomin, Matteo Trobinger, Amy L. Murphy, Gian Pietro Picco.  In Proceedings of the International Conference on Information Processing in Sensor Networks (IPSN 2018), to appear.


#Build instructions

cd simulations
./make_Arctium_26_nodes.sh

which will create the executables for 26 sensor nodes as well as the 26_nodes_crystal_method_gm.csc simulation file which can be run in Cooja.


