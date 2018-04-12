#!/usr/bin/ruby

def show_options
  puts "--------------------------- Options ---------------------------"
  puts "-i : Initialise the file system for Pintos"
  puts "-x : Execute test program given by next argument (path)"
  puts "-m : Make the .result file for the given test (path)"
  puts
  puts "NOTE: This script must be ran from .../src/userprog"
  puts "---------------------------------------------------------------"
end

def change_dir_down
  # Change directory
  puts "  Changing directory to userprog/build..."
  `cd build/`
  puts "  ... complete."
end #def change_dir_down

def change_dir_up
  # Change directory
  puts "  Changing directory to userprog..."
  `cd ..`
  puts "  ... complete."
end #def change_dir_up

def initialise
  # Initialise file-system
  puts "Initialising Pintos file-system..."

  # Make
  puts "  Calling \'make\'..."
  `make`
  puts "  ... complete (text hidden)."

  # Change directory
  change_dir_down()

  # Create disk
  puts "  Creating disk..."
  puts `pintos-mkdisk filesys.dsk --filesys-size=2`
  puts "  ... complete."

  # Format disk
  puts "  Formatting disk..."
  puts `pintos -f -q`
  puts "  ... complete."

  # Change directory
  change_dir_up()

  puts "... complete."
end #def initialise

def execute(path, args)
  # Execute file at path
  puts "Executing #{path}"

  # Make in src/examples
  puts "  Calling \'make\'..."
  `cd ../examples/`
  `make`
  `cd ../userprog/`
  puts "  ... complete (text hidden)."

  # Change directory
  change_dir_down()

  # Load program onto disk
  puts "  Loading program onto disk..."
  extended_path = "../" + path
  name = path.split('/')[-1]
  puts `pintos -p #{extended_path} -a #{name} -- -q`
  puts "  ... complete."

  # Run program
  puts "  Running program..."
  puts `pintos -q run #{args}`
  puts "  ... complete."

  puts "... complete."
end #def execute


path = `pwd`
if path.split('/')[-1] <=> "userprog"

  option = ARGV.shift
  puts option
  if option.eql? "-i"
    initialise()
  elsif option.eql? "-x"
    args = ""
    next_word = ARGV.shift
    while next_word != nil
      args = args + next_word + " "
      next_word = ARGV.shift
    end
    execute(ARGV.shift, args)
  elsif option.eql? "-m"
    make_result(ARGV.shift)
  else
    if option != nil
      puts "Invalid option: #{option}"
    end #option != nil
    show_options()
  end

else #path.split('/')[-1] != "userprog"

  puts "NOTE: This script must be ran from .../src/userprog"

end #path.split('/')[-1] != "userprog"
