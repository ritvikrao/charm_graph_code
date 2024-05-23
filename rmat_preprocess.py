if __name__ == "__main__":
    file_input_name = input("Enter the name of the file to preprocess: ")
    file_output_name = input("Enter the name of the file to write to: ")
    with open(file_input_name, 'r') as in_file:
        with open(file_output_name, 'w') as out_file:
            for line in in_file:
                nums = line.split()
                print("First word: ", nums[0])
                print("Second word: ", nums[1])
                new_line = nums[0] + ',' + nums[1]
                out_file.write(new_line)
                out_file.write('\n')