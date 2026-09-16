import os
import sys

def convert_data_to_header(binary_path, header_path, array_name):
    with open(binary_path, 'rb') as binary_file:
        binary_data = binary_file.read()

    with open(header_path, 'w') as header_file:
        header_file.write('#ifndef {}_h\n'.format(array_name.lower()))
        header_file.write('#define {}_h\n\n'.format(array_name.lower()))
        header_file.write('#include <stdint.h>\n\n')
        header_file.write('static const uint8_t {}[] = {{\n'.format(array_name))

        for i in range(0, len(binary_data), 12):
            line = ', '.join('0x{:02x}'.format(byte) for byte in binary_data[i:i+12])
            header_file.write('    {},\n'.format(line))

        header_file.write('};\n\n')
        header_file.write('#endif /* {}_h */\n'.format(array_name.lower()))

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: python data2header.py <binary_file> <header_file> <array_name>")
        sys.exit(1)

    binary_path = sys.argv[1]
    header_path = sys.argv[2]
    array_name = sys.argv[3]

    convert_data_to_header(binary_path, header_path, array_name)

