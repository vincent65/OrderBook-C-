#include<iostream>
#include<map>
#include<set>
#include<list>
#include<cmath>
#include<ctime>
#include<deque>
#include<queue>
#include<stack>
#include<limits>
#include<string>
#include<vector>
#include<numeric>
#include<iostream>
#include<algorithm>
#include<unordered_map>
#include<memory>
#include<variant>
#include<optional>
#include<tuple>
#include<format>

enum class OrderType {
    GoodTillCancel,
    FillAndKill
};

enum class Side {
    Buy, Sell
};


using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

//gets information about the state of the order book
struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos{
    public:
        OrderBookLevelInfos(const LevelInfos& bids, const LevelInfos& asks) {
            bids_ = bids;
            asks_ = asks;
        }

        const LevelInfos& GetBids() const { return bids_;}
        const LevelInfos& GetAsks() const {return asks_;}

    private:
        LevelInfos bids_;
        LevelInfos asks_;
};

class Order {
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

        OrderId GetOrderId() const { return orderId_; }
        Side GetSide() const { return side_; }
        Price GetPrice() const { return price_; }
        OrderType GetOrderType() const { return orderType_; }
        Quantity GetInitialQuantity() const { return initialQuantity_; }
        Quantity GetRemainingQuantity() const { return remainingQuantity_; }

        Quantity GetFilledQuantity() const {return GetInitialQuantity() - GetRemainingQuantity();}
        bool isFilled() const {return GetRemainingQuantity() == 0;}

        void Fill(Quantity quantity) {
            if(quantity > GetRemainingQuantity()) {
                throw std::logic_error("Order ({}) cannot be filled for more than its remaining quantity. ");
            }

            remainingQuantity_ -= quantity;
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
//we use list because list gives iterator that cannot be invalidated regardless of size; this is useful to determine where our order is in the bid or ask book
//list is dispersed in memeroy while veector is together
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

        OrderPointer ToOrderPointer(OrderType type) const {
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
        Trade(const TradeInfo& bidTrade, const TradeInfo& askTrade)
        {
            bidTrade_ = bidTrade;
            askTrade_ = askTrade;
        }

        const TradeInfo& GetBidTrade() const {return bidTrade_;}

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
        std::map<Price, OrderPointers, std::greater<Price> > bids_;
        std::map<Price, OrderPointers, std::less<Price> > asks_;
        std::unordered_map<OrderId, OrderEntry> orders_;

        bool CanMatch(Side side, Price price) const
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

        Trades MatchOrders() 
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

                    if(bid->isFilled()) 
                    {
                        bids.pop_front();
                        orders_.erase(bid->GetOrderId());
                    }
                    if(ask->isFilled()) 
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

    public:

        Trades AddOrder(OrderPointer order)
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

        void CancelOrder(OrderId orderId) {
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

        Trades ModifyOrder(OrderModify order) {
            if(orders_.find(order.GetOrderId()) == orders_.end()){
                return { };
            }
            const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
            CancelOrder(order.GetOrderId());
            return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
        }

        std::size_t Size() const {return orders_.size();}

        OrderBookLevelInfos GetOrderInfos() const 
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

            return OrderBookLevelInfos{bidInfos, askInfos};
        }
};

int main(){
    OrderBook orderbook;
    const OrderId orderId = 1;
    orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));

    std::cout << orderbook.Size() << std::endl;

    orderbook.CancelOrder(orderId);

    std::cout << orderbook.Size() <<std::endl;

    return 0;
}