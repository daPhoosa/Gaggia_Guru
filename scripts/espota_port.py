"""
PlatformIO post script for the esp32-ota environment.

espota.py listens on --host_port for the ESP32 to connect back, but it doesn't set
SO_REUSEADDR, so after one upload the port sits in TIME_WAIT for ~60 s and a second
upload (e.g. `upload` then `uploadfs`) fails with "Listen Failed".

Right before each upload this picks the first free port in HOST_PORTS and passes
it as --host_port. Allow the whole range in the PC firewall:
    sudo ufw allow from 192.168.1.0/24 to any port 3233:3240 proto tcp
"""
import socket

Import("env")  # noqa: F821

HOST_PORTS = range(3233, 3241)


def free_port():
    for port in HOST_PORTS:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.bind(("0.0.0.0", port))  # no SO_REUSEADDR, same as espota
            return port
        except OSError:
            continue
        finally:
            s.close()
    return None


def pick_host_port(source, target, env):
    port = free_port()
    if port is None:
        print(f"[espota] No free host port in {HOST_PORTS.start}-{HOST_PORTS.stop - 1}; wait a minute and retry")
        env.Exit(1)
    flags = [f for f in env["UPLOADERFLAGS"] if not str(f).startswith("--host_port")]
    env.Replace(UPLOADERFLAGS=flags + [f"--host_port={port}"])
    print(f"[espota] Using host port {port}")


env.AddPreAction("upload", pick_host_port)  # noqa: F821
env.AddPreAction("uploadfs", pick_host_port)  # noqa: F821
