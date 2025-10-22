# CS 2520: Project 2 

This directory includes starter code for your Project 1 and instructions for
testing under emulated network conditions using Docker.

## Docker setup

We have provided a `Dockerfile` that can be used to set up a Docker image with
all of the necessary dependencies.

To build the image, run:
```
docker build . -t "cs2520_proj2"
```

**Note:** If the build fails with an error on the `apt install` commands in the
Dockerfile, it is likely due to cached results from a previous build being
used. In that case, use the following instead:
```
docker build --no-cache -t "cs2520_proj2" .
```

We will test your projects using a Docker [bridge
network](https://docs.docker.com/network/network-tutorial-standalone/). To
create a bridge network named `cs2520`, you can run (on your host, not inside a
container):
```
docker network create --driver bridge cs2520
```

Then, to create and interactively run two containers `rcv` and `ncp` that both
connect to the bridge, you can run (in two separate terminal windows):
```
docker run -it --cap-add=NET_ADMIN --name rt_srv --network cs2520 cs2520_proj2
docker run -it --cap-add=NET_ADMIN --name rt_rcv --network cs2520 cs2520_proj2
```

With this setup, you can use the names of the containers (i.e. `rt_srv` and
`rt_rcv`) as the hostnames for communication. Alternatively, you can find the
IP address for each container by running `ip addr` in the container and then
use the IP addresses.

Note that the `NET_ADMIN` capability is needed to emulate WAN characteristics
using the netem tool (see next section).

## Opening additional shells

To open additional shells on the same container (e.g. you may have one shell
for the `udp_stream` tool and one for your real-time transfer tool), you can
use the docker exec command.

For example, to open a new shell on the `rt_srv` container, run the following
(in a new terminal window):
```
docker exec -it rt_srv bash
```

To open a new shell on the `rt_rcv` container, run the following
(in a new terminal window):
```
docker exec -it rt_rcv bash
```

## Network emulation

Here we provide instructions for emulating the network conditions under which you will test your programs. You can find more detailed information in the man pages for [tc](https://man7.org/linux/man-pages/man8/tc.8.html) and [netem](https://man7.org/linux/man-pages/man8/tc-netem.8.html)

### Emulating Delay

For your experiments, you should emulate a delay of 20ms on the link between
your containers. To do this you should use the following command on each of
your containers:
```
tc qdisc add dev eth0 root netem delay 20ms
```

To test that the delay was added successfully, you can use the `ping` tool. For
example, from your `rt_rcv` container, you can run:
```
ping rt_srv
```

You should see a delay of about 40ms.

### Removing Emulated Bandwidth Constraints and Delay

To remove all emulated network characteristics, you can use the command:
```
tc qdisc del dev eth0 root
```

## Docker cleanup

When you are done, remove both containers:
```
docker rm rt_rcv
docker rm rt_srv
```

And remove the network:
```
docker network rm cs2520
```
