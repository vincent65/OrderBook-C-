#include <iostream>
#include <map>
#include <set>
#include <list>
#include <cmath>
#include <ctime>
#include <deque>
#include <queue>
#include <stack>
#include <limits>
#include <string>
#include <vector>
#include <numeric>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <variant>
#include <optional>
#include <tuple>
#include <format>
#include <stdio.h>
#include <stdlib.h>

#include<mutex>
#include<ctime>
#include <thread>
#include <condition_variable>
#include <numeric>
#include <chrono>
#include<time.h>


struct Constants
{
    static const Price InvalidPrice = std::numeric_limits<Price>::quiet_NaN();
};
enum class OrderType
{
    GoodTillCancel,
    FillAndKill,
    FillOrKill,
    GoodForDay,
    Market,
};

enum class Side
{
    Buy,
    Sell
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using OrderIds = std::vector<OrderId>;

// gets information about the state of the order book
struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos
{
public:
    OrderBookLevelInfos(const LevelInfos &bids, const LevelInfos &asks)
    {
        bids_ = bids;
        asks_ = asks;
    }

    const LevelInfos &GetBids() const { return bids_; }
    const LevelInfos &GetAsks() const { return asks_; }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class Order
{
public:
    Order(OrderType orderType, OrderId orderId, Side side, Price price, Quantity quantity)
    {
        orderType_ = orderType;
        orderId_ = orderId;
        side_ = side;
        price_ = price;
        initialQuantity_ = quantity;
        remainingQuantity_ = quantity;
    }

    //constructor for market orders
    Order(OrderId orderId, Side side, Quantity quantity){
        orderType_ = OrderType::Market;
        orderId_ = orderId;
        side_ = side;
        price_ = Constants::InvalidPrice;   //price doesnt matter for market orders
        initialQuantity_ = quantity;
        remainingQuantity_ = quantity;
    }

    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    OrderType GetOrderType() const { return orderType_; }
    Quantity GetInitialQuantity() const { return initialQuantity_; }
    Quantity GetRemainingQuantity() const { return remainingQuantity_; }

    Quantity GetFilledQuantity() const { return GetInitialQuantity() - GetRemainingQuantity(); }
    bool isFilled() const { return GetRemainingQuantity() == 0; }

    void Fill(Quantity quantity)
    {
        if (quantity > GetRemainingQuantity())
        {
            throw std::logic_error("Order ({}) cannot be filled for more than its remaining quantity. ");
        }

        remainingQuantity_ -= quantity;
    }
    
    void ToGoodTillCancel(Price price) {
        if(orderType_ != OrderType::Market) {
            throw std::logic_error("cannot convert unless market order");
        }
        price_ = price;
        orderType_ = OrderType::GoodTillCancel;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

//
using OrderPointer = std::shared_ptr<Order>;
// we use list because list gives iterator that cannot be invalidated regardless of size; this is useful to determine where our order is in the bid or ask book
// list is dispersed in memeroy while veector is together
using OrderPointers = std::list<OrderPointer>;

class OrderModify
{
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity)
    {
        orderId_ = orderId;
        side_ = side;
        price_ = price;
        quantity_ = quantity;
    }
    OrderId GetOrderId() const { return orderId_; }
    Side GetSide() const { return side_; }
    Price GetPrice() const { return price_; }
    Quantity GetQuantity() const { return quantity_; }

    OrderPointer ToOrderPointer(OrderType type) const
    {
        return std::make_shared<Order>(type, orderId_, side_, price_, quantity_);
    }

private:
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;
};

struct TradeInfo
{
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade
{
public:
    Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade)
    {
        bidTrade_ = bidTrade;
        askTrade_ = askTrade;
    }

    const TradeInfo &GetBidTrade() const { return bidTrade_; }

    const TradeInfo &GetAskTrade() const { return askTrade_; }

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

class OrderBook
{
private:
    struct OrderEntry
    {
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_;
    };
    struct LeveLData
    {
        Quantity quantity_ { };
        Quantity count_ { };

        enum class Action 
        {
            Add, Remove, Match,
        };
    };

    std::unordered_map<Price, OrderBook::LeveLData> data_;

    std::map<Price, OrderPointers, std::greater<Price>> bids_;
    std::map<Price, OrderPointers, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    //to handle GoodForDay Orders, we need to implement a system that keeps track of the time of orders
    
    mutable std::mutex ordersMutex_;
    std::thread ordersPruneThread_;
    std::condition_variable shutDownConditionVariable_;
    std::atomic<bool> shutdown_ {false};

    // we don't want this thread which operates separate from our main thread to modifty our data structure at the same time
    //we need to use a lock and a mutex to protect
    void PruneGoodForDayOrders(){
        using namespace std::chrono;

        const auto end = hours(16);

        while(true) {
            //check if it is 4PM yet
            const auto now = system_clock::now();
            const auto now_t = system_clock::to_time_t(now);
            std::tm now_parts;
            //?
            localtime_r(&now_t, &now_parts);

            if(now_parts.tm_hour >= end.count()){
                now_parts.tm_mday += 1;
            }

            now_parts.tm_hour = end.count();
            now_parts.tm_min = 0;
            now_parts.tm_sec = 0;

            //how much longer do I have to wait until 4PM?
            auto next = system_clock::from_time_t(mktime(&now_parts));
            auto till4 = next - now + milliseconds(120);

            {
                std::unique_lock ordersLock(ordersMutex_);
                //if our orderbook is shutdown, we don't prune anything
                //we have to wait until 4PM to do anything with canceling
                if(shutdown_.load(std::memory_order_acquire) || shutDownConditionVariable_.wait_for(ordersLock, till4) == std::cv_status::no_timeout){
                    return;
                }
            }

            //we made it to 4PM without any shutting down so we are can cancel the orders
            OrderIds orderIds;
            {
                std::scoped_lock ordersLock(ordersMutex_);
                for(const auto& [_, entry] : orders_) {
                    const auto& [order, _] = entry;
                    if(order->GetOrderType() == OrderType::GoodForDay) {
                        continue;
                    }
                    orderIds.push_back(order->GetOrderId());
                }
            }

            CancelOrders(orderIds);
        }
    }

    void CancelOrders(OrderIds orderIds){
        std::scoped_lock ordersLock(ordersMutex_);

        for(const auto& orderId: orderIds) {
            //if you just call the regular CancelOrder, you would be taking and releasing the mutex many times, this will cause a lot of traffic
            //this is because our datastructure is protected by the orders mutex
            //so it is better to call CancelOrderInternal
            CancelOrderInternal(orderId);
        }
    }

    void CancelOrderInternal(OrderId orderId) {
        if (orders_.find(orderId) == orders_.end())
        {
            return;
        }

        const auto &[order, orderIterator] = orders_.at(orderId);
        orders_.erase(orderId);

        if (order->GetSide() == Side::Sell)
        {
            auto price = order->GetPrice();
            auto &orders = asks_.at(price);
            orders.erase(orderIterator);
            if (orders.empty())
            {
                asks_.erase(price);
            }
        }
        else
        {
            auto price = order->GetPrice();
            auto &orders = bids_.at(price);
            orders.erase(orderIterator);
            if (orders.empty())
            {
                bids_.erase(price);
            }
        }
        OnOrderCancelled(order);
    }
    //when apis are written according to events, makes code cleaner
    void OnOrderCancelled(OrderPointer order){
        UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LeveLData::Action::Remove);
    }
    void OnOrderAdded(OrderPointer order){
        UpdateLevelData(order->GetPrice(), order->GetInitialQuantity(), LeveLData::Action::Add);
    }
    void onOrderMatched(Price price, Quantity quantity, bool isFullyFilled) {
        UpdateLevelData(price, quantity, isFullyFilled ? LeveLData::Action::Remove : LeveLData::Action::Match);
    }

    void UpdateLevelData(Price price, Quantity quantity, LeveLData::Action action) {
        auto&data = data_[price];

        data.count_ += action == LeveLData::Action::Remove ? -1 : action == LeveLData::Action::Add ? 1 : 0;
        if(action == LeveLData::Action::Remove || action == LeveLData::Action::Match) {
            data.quantity_ -= quantity;
        }
        else{
            data.quantity_ += quantity;
        }
        if(data.count_ == 0) {
            data_.erase(price);
        }
    }

    //fill or kill
    bool CanFullyFill(Side side, Price price, Quantity quantity) const {
        if(!CanMatch(side, price)){
            return false;
        }
        std::optional<Price> threshold;
        if(side == Side::Buy) {
            const auto [askPrice, _] = *asks_.begin();
            threshold = askPrice;
        }
        else{
            const auto[bidPrice, _] = *bids_.begin();
            threshold = bidPrice;
        }
        
        for (const auto& [levelPrice, levelData] : data_) {
            if(threshold.has_value() && (side==Side::Buy && threshold.value() > levelPrice) || (side==Side::Sell && threshold.value() < levelPrice)){
                continue;
            }
            if ((side == Side::Buy && threshold.value() < levelPrice) || (side == Side::Sell && threshold.value() > levelPrice)){
                continue;
            }
            if(quantity <= levelData.quantity_) {
                return true;
            }
            quantity -= levelData.quantity_;
        }
        return false;
    }

    bool CanMatch(Side side, Price price) const
    {
        if (side == Side::Buy)
        {
            if (asks_.empty())
            {
                return false;
            }

            // check if buy order has price greater than or equal to the best ask
            const auto &[bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        }
        else
        {
            if (bids_.empty())
            {
                return false;
            }
            // check if sell order has price less than or equal to the best bid
            const auto &[bestBid, _] = *bids_.begin();
            return price <= bestBid;
        }
    }

    Trades MatchOrders()
    {
        Trades trades;
        trades.reserve(orders_.size());

        while (true)
        {
            if (bids_.empty() || asks_.empty())
            {
                break;
            }
            auto &[bidPrice, bids] = *bids_.begin();
            auto &[askPrice, asks] = *asks_.begin();

            if (bidPrice < askPrice)
            {
                break;
            }

            while (bids.size() && asks.size())
            {
                auto &bid = bids.front();
                auto &ask = asks.front();
                Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->isFilled())
                {
                    bids.pop_front();
                    orders_.erase(bid->GetOrderId());
                }
                if (ask->isFilled())
                {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                }

                if (bids.empty())
                {
                    bids_.erase(bidPrice);
                }
                if (asks.empty())
                {
                    asks_.erase(askPrice);
                }

                trades.push_back(Trade{TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity}, TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}});
                onOrderMatched(bid->GetPrice(), quantity, bid->isFilled());
                onOrderMatched(ask->GetPrice(), quantity, ask->isFilled());
            }
        }

        if (!bids_.empty())
        {
            auto &[_, bids] = *bids_.begin();
            auto &order = bids.front();
            if (order->GetOrderType() == OrderType::FillAndKill)
            {
                CancelOrder(order->GetOrderId());
            }
        }

        if (!asks_.empty())
        {
            auto [_, asks] = *asks_.begin();
            auto &order = asks.front();
            if (order->GetOrderType() == OrderType::FillAndKill)
            {
                CancelOrder(order->GetOrderId());
            }
        }

        return trades;
    }

public:
    //instantiate thread that waits until the end of day and then cancels all GoodForDay Orders at the end of day
    //thread runs in background in infinite loop on PruneGoodForDayorders
    OrderBook() : ordersPruneThread_ {[this] {PruneGoodForDayOrders(); } }  {

    }
    //destructor that deletes an instance
    ~OrderBook() {
        shutdown_.store(true, std::memory_order_release);
        shutDownConditionVariable_.notify_one();
        ordersPruneThread_.join();
    }
    Trades AddOrder(OrderPointer order)
    {
        // contains
        if (orders_.find(order->GetOrderId()) != orders_.end())
        {
            return {};
        }

        //we essentially turn market orders into limit orders that have the worst price available in the orderbook so they can execute immediately
        if(order->GetOrderType() == OrderType::Market){
            if(order->GetSide() == Side::Buy && !asks_.empty()){
                const auto& [worstAsk, _] = *asks_.rbegin();
                order->ToGoodTillCancel(worstAsk);
            }
            else if (order->GetSide() == Side::Sell && !bids_.empty()){
                const auto& [worstBid, _] = *asks_.rbegin();
                order->ToGoodTillCancel(worstBid);
            }
            else{
                return {};
            }
        }

        //if we cannot fulfill the order immediately, then we have to kill the order (Fill and kill)
        if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
        {
            return {};
        }
        
        //fill and kill
        if(order->GetOrderType() == OrderType::FillAndKill && !CanFullyFill(order->GetSide(), order->GetPrice(), order->GetInitialQuantity()));

        OrderPointers::iterator iterator;

        if (order->GetSide() == Side::Buy)
        {
            auto &orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }
        else
        {
            auto &orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        orders_.insert({order->GetOrderId(), OrderEntry{order, iterator}});

        OnOrderAdded(order);

        return MatchOrders();
    }

    void CancelOrder(OrderId orderId)
    {
        std::scoped_lock ordersLock(ordersMutex_);
        CancelOrderInternal(orderId);
    }

    Trades ModifyOrder(OrderModify order)
    {
        OrderType ot;
        {
            std::scoped_lock ordersLock {ordersMutex_};
            if (orders_.find(order.GetOrderId()) == orders_.end())
            {
                return {};
            }
            const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
            ot = existingOrder->GetOrderType();
        }
        
        CancelOrder(order.GetOrderId());
        return AddOrder(order.ToOrderPointer(ot));
    }

    std::size_t Size() const { 
        std::scoped_lock ordersLock {ordersMutex_};    
        return orders_.size(); 
    }

    OrderBookLevelInfos GetOrderInfos() const
    {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size());
        askInfos.reserve(orders_.size());

        // lambda function that creates a "level" for each price
        auto CreateLevelInfos = [](Price price, const OrderPointers &orders)
        {
            return LevelInfo{price, std::accumulate(orders.begin(), orders.end(), (Quantity)0,
                                                    [](Quantity runningSum, const OrderPointer &order)
                                                    { return runningSum + order->GetRemainingQuantity(); })};
        };

        for (const auto &[price, orders] : bids_)
        {
            bidInfos.push_back(CreateLevelInfos(price, orders));
        }
        for (const auto &[price, orders] : asks_)
        {
            askInfos.push_back(CreateLevelInfos(price, orders));
        }

        return OrderBookLevelInfos{bidInfos, askInfos};
    }
};

int main()
{
    OrderBook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));

    std::cout << orderbook.Size() << std::endl;

    orderbook.CancelOrder(orderId);

    std::cout << orderbook.Size() << std::endl;

    return 0;
}