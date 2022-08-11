#ifndef __NOCOPYABLE_H__
#define __NOCOPYABLE_H__
class nocopyable
{
protected:
    nocopyable() {}
    ~nocopyable() {}

private:
    nocopyable(const nocopyable &);
    const nocopyable &operator=(const nocopyable &);
};

#endif