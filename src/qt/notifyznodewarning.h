#ifndef ZCOIN_NOTIFYZNODEWARNING_H
#define ZCOIN_NOTIFYZNODEWARNING_H

class NotifyTnodeWarning
{
public:

    ~NotifyTnodeWarning();

    static void notify();
    static bool shouldShow();
    static bool nConsidered;
};

#endif //ZCOIN_NOTIFYZNODEWARNING_H
