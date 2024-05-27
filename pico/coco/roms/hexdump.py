import sys
import os

def hexdump(filename):
    fout = sys.stdout
    ll = os.path.getsize(filename)
    c=0
    with open(filename, 'rb') as file:
        byte = file.read(1)  # Read one byte at a time
        print(f'unsigned char __attribute__((aligned({ll}))) rom[{ll}] =','{',file=fout) 
        print('   ',hex(int.from_bytes(byte, byteorder='little')),end='',file=fout)
        byte = file.read(1)  # Read next byte
        while byte:
            c+=1
            print(', ',end='',file=fout)
            if c % 16 == 0:
                print('\n    ',end='',file=fout)
            print(hex(int.from_bytes(byte, byteorder='little')),end='',file=fout)  # Convert byte to hexadecimal and print without '0x'
            byte = file.read(1)  # Read next byte
        print('\n    };',file=fout)  # Print a newline after all bytes have been processed

# Example usage:
if __name__ == "__main__":
    # Accessing command-line arguments
    arguments = sys.argv
    if len(arguments) > 1:
        filename = arguments[1]
    else:
        sys.exit("must specify rom filename")

    hexdump(filename)
