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
