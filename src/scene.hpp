#ifndef scene_hpp
#define scene_hpp

#include "light.hpp"

class Scene {
public:
    // must be thread-safe
    virtual color calculate_color(int x,int y,int w,int h) const = 0;

    /* Prevent python code from modifying the scene. The object is also expected
       to remain alive until unlock is called */
    virtual void lock() = 0;
    
    virtual void unlock() throw() = 0;
};

#endif