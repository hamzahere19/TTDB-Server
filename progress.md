# Progress Report - 30th Sept

 Set up VS Code environment on Linux for project development.
 Thoroughly reviewed and understood the complete project requirements and architecture.

 Stack template class (push, pop, peek, isEmpty, depth, snapshot_into).
  Timeline doubly linked list class (record, begin, getStepCount).
 Implemented Pass 0x0 
   Created readSourceLine, firstWord, and secondWord helper functions 
   Implemented program structural validation nested functions check, unmatched func_end check, out-of-function code check, and missing main check.


  # 1st october

  I know binary filing in c++ but pass 0x1 conatin FILE* 
  used as a paramter in helping function is a pointer structure in C language so learn about binary reading and writing in C language . Implement writeresolverecord function


  # 2nd october

  implement readresolvefunction with C lanuage binary filing.
  Implement resolveProgram which converts a text source file into a binary program file by scanning all lines, recording function entry locations and function call positions, replacing unresolved call references with their actual function addresses, and returning the entry point offset of the main function.

 # 3rd october

 implement tokenizeline of pass 0x2 . Separate line into keyword , indentifier and param.
 Implement buildsnapshot that calls snapshot_into func which copies and save current stack state.

 # 4th october

 Implement executeprogram in which i create stack and frame and push frame into stack then read the line from target file and tokenize it and access active fram pointer using peek and then apply SET , CALL and FUNC_END instructions into it. Implemented all the instructions includes variables management , function call and function return.

  # 5th october

  implemented the writeTdbg function to save timeline snapshots and their byte positions into a binary .tdbg file.
  Saved an index array of byte offsets at the end of the file and updated the header to enable instant jumping to any step in the debugger.

  # 6th and 7th october 

  Almost found around 25 bugs in my code.Most of them were type mismatch like passing a variable to a function but function paramter is of different type and many other logical bugs. Still have the work to do.


  # 8th - 10th october
  
  Add , mul , div was not implemented.They were only in a comment.There was no check for division by zero. If locals were more than 16, the code wrote outside the array.I added three helper functions:
  findVar: finds a variable in locals and args
  getValue: gives the number from a number or a variable
  setVar: updates a variable, or creates it if it is not found
  set now also uses getValue, so set x y works.
  div now checks for zero. If the value is zero, it prints an error and stops.

  Running final checks on my code and testing the output.


