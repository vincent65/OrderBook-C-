#pragma once

#include <map>
#include <unordered_map>
#include <thread>
#include <condition_variable>
#include <mutex>

#include "Usings.h"
#include "Order.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "Trade.h"

class Orderbook
{
    private:
        struct OrderEntry
        {
            OrderPointer order_{nullptr};
            OrderPointers::iterator location_;
        };
        
        
        
        std::map<Price, OrderPointers, std::greater<Price>> bids_;
        std::map<Price, OrderPointers, std::less<Price>> asks_;
        std::unordered_map<OrderId, OrderEntry> orders_;



        bool CanMatch(Side side, Price price) const;
        Trades MatchOrders();

    public:
        Orderbook();
        Trades AddOrder(OrderPointer order);
        void CancelOrder(OrderId orderId);
        Trades ModifyOrder(OrderModify order);
        std::size_t Size() const;
        OrderbookLevelInfos GetOrderInfos() const;

};