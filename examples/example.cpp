#include <iostream>
#include <sstream>

#include "stack.hpp"

int main()
{
    jcd::stack_t<int> s;
    for( std::string line; std::getline( std::cin, line ); ) {
        std::istringstream stream{ line };
        if( char op; stream >> op ) {
            if( op == 'e' ) {
                std::cout << std::boolalpha << s.empty();
            } 
            else if( op == 'q' ) {
                break;
            }
        }
    }
    
    return 0;
}
