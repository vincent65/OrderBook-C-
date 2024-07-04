#include "Orderbook.h"

#include<numeric>
#include<chrono>
#include<ctime>

bool Orderbook::CanMatch(Side side, Price price) const
{
    if(side == Side::Buy) {
        if(asks_.empty()) {
            return false;
        }

        //check if buy order has price greater than or equal to the best ask
        const auto& [bestAsk, _] = *asks_.begin();
        return price >= bestAsk;
    }
    else{
        if(bids_.empty()){
            return false;
        }
        //check if sell order has price less than or equal to the best bid
        const auto& [bestBid, _] = *bids_.begin();
        return price <= bestBid;
    }
}

Trades Orderbook::MatchOrders() 
{
    Trades trades;
    trades.reserve(orders_.size());

    while(true){
        if(bids_.empty() || asks_.empty()) {
            break;
        }
        auto& [bidPrice, bids] = *bids_.begin();
        auto& [askPrice, asks] = *asks_.begin();

        if(bidPrice < askPrice) {
            break;
        }

        while(bids.size() && asks.size()) {
            auto& bid = bids.front();
            auto& ask = asks.front();
            Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
            bid->Fill(quantity);
            ask->Fill(quantity);

            if(bid->IsFilled()) 
            {
                bids.pop_front();
                orders_.erase(bid->GetOrderId());
            }
            if(ask->IsFilled()) 
            {
                asks.pop_front();
                orders_.erase(ask->GetOrderId());
            }

            if(bids.empty()){
                bids_.erase(bidPrice);
            }
            if(asks.empty()){
                asks_.erase(askPrice);
            }

            trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity}, TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});
        }
    }

    if(!bids_.empty()) {
        auto& [_, bids] = *bids_.begin();
        auto& order = bids.front();
        if(order->GetOrderType() == OrderType::FillAndKill){
            CancelOrder(order->GetOrderId());
        }
    }

    if(!asks_.empty()) {
        auto [_, asks] = *asks_.begin();
        auto& order = asks.front();
        if(order->GetOrderType() == OrderType::FillAndKill) {
            CancelOrder(order->GetOrderId());
        }
    }

    return trades;
}


Trades Orderbook::AddOrder(OrderPointer order)
{
    //contains
    if(orders_.find(order->GetOrderId()) != orders_.end()){
        return {};
    }

    if(order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())){
        return {};
    }

    OrderPointers::iterator iterator;

    if(order->GetSide() == Side::Buy) {
        auto& orders = bids_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::next(orders.begin(), orders.size() - 1);
    }
    else{
        auto &orders = asks_[order->GetPrice()];
        orders.push_back(order);
        iterator = std::next(orders.begin(), orders.size() - 1);
    }

    orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

    return MatchOrders();
}

void Orderbook::CancelOrder(OrderId orderId) {
    if(orders_.find(orderId) == orders_.end()) {
        return;
    }

    const auto& [order, orderIterator] = orders_.at(orderId);
    orders_.erase(orderId);

    if(order->GetSide() == Side::Sell) {
        auto price = order->GetPrice();
        auto& orders = asks_.at(price);
        orders.erase(orderIterator);
        if(orders.empty()) {
            asks_.erase(price);
        }
    }
    else{
        auto price = order->GetPrice();
        auto &orders = bids_.at(price);
        orders.erase(orderIterator);
        if (orders.empty())
        {
            bids_.erase(price);
        }
    }
}

Trades Orderbook::ModifyOrder(OrderModify order) {
    if(orders_.find(order.GetOrderId()) == orders_.end()){
        return { };
    }
    const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
    CancelOrder(order.GetOrderId());
    return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
}

std::size_t Orderbook::Size() const {return orders_.size();}

OrderbookLevelInfos Orderbook::GetOrderInfos() const 
{
    LevelInfos bidInfos, askInfos;
    bidInfos.reserve(orders_.size());
    askInfos.reserve(orders_.size());

    //lambda function that creates a "level" for each price
    auto CreateLevelInfos = [](Price price, const OrderPointers& orders)
    {
        return LevelInfo{price, std::accumulate(orders.begin(), orders.end(), (Quantity)0, 
        [](Quantity runningSum, const OrderPointer& order)
            {return runningSum + order->GetRemainingQuantity(); } ) };
    };

    for(const auto& [price, orders] : bids_) {
        bidInfos.push_back(CreateLevelInfos(price, orders));
    }
    for(const auto& [price, orders] : asks_) {
        askInfos.push_back(CreateLevelInfos(price, orders));
    }

    return OrderbookLevelInfos{bidInfos, askInfos};
}


