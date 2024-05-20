FROM gcc:14.1.0

# Update the package lists
RUN apt-get update

# Install gdb and nasm
RUN apt-get install -y gdb nasm

# Command to keep the container running
CMD tail -f /dev/null
