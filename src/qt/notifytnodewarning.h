#ifndef ZCOIN_NOTIFYTNODEWARNING_H
#define ZCOIN_NOTIFYTNODEWARNING_H

class NotifyTnodeWarning
{
public:

    ~NotifyTnodeWarning();

    static void notify();
    static bool shouldShow();
    static bool nConsidered;
};

#endif //ZCOIN_NOTIFYTNODEWARNING_H
