# Geometric Brownian Motion (GBM)

## 1. Mathematical Background

### Stochastic Differential Equation (SDE)

Stock prices are modeled using Geometric Brownian Motion:

```
dS = μ · S · dt + σ · S · dW
```

Where:
- **S** = Current stock price
- **μ** (mu) = Drift coefficient (expected return rate)
- **σ** (sigma) = Volatility coefficient
- **dt** = Time step
- **dW** = Wiener process increment (random shock)

### Discretization for Simulation

For computer simulation, we discretize the continuous equation:

```
S(t+dt) = S(t) + μ · S(t) · dt + σ · S(t) · √dt · Z
```

Where Z ~ N(0,1) is a standard normal random variable.

Alternatively, using the exponential form (more accurate for large moves):

```
S(t+dt) = S(t) · exp((μ - σ²/2) · dt + σ · √dt · Z)
```

### Why GBM for Stock Prices?

1. **Positivity**: Prices never go negative (multiplicative model)
2. **Log-returns are normal**: Matches empirical observations
3. **Independence**: Future returns don't depend on past prices
4. **Scalability**: Percentage moves, not absolute moves

---

## 2. Implementation Details

### Box-Muller Transform

To generate normal random variables efficiently, we use the Box-Muller transform:

Given U₁, U₂ ~ Uniform(0,1):

```
Z₀ = √(-2 · ln(U₁)) · cos(2π · U₂)
Z₁ = √(-2 · ln(U₁)) · sin(2π · U₂)
```

Both Z₀ and Z₁ are independent N(0,1) random variables.

**Implementation:**
```cpp
double generate_normal() {
    double u1 = uniform_dist(rng);
    double u2 = uniform_dist(rng);
    
    double mag = sqrt(-2.0 * log(u1));
    double z0 = mag * cos(2.0 * M_PI * u2);
    // z1 = mag * sin(...) saved for next call
    
    return z0;
}
```

### Parameter Selection

| Parameter | Value Range | Rationale |
|-----------|-------------|-----------|
| **μ (drift)** | -0.05 to +0.05 | Annual drift, 0 for neutral market |
| **σ (volatility)** | 0.01 to 0.06 | Low-vol stocks: 0.01-0.02, High-vol: 0.04-0.06 |
| **dt (time step)** | 0.001 | 1ms resolution for high-frequency simulation |
| **Initial Price** | ₹100 - ₹5000 | Realistic NSE stock price range |

### Price Update Algorithm

```cpp
void update_price(SymbolState& symbol) {
    // Generate Wiener process increment
    double dW = generate_normal() * sqrt(dt);
    
    // Apply GBM equation
    double drift_term = symbol.drift * symbol.price * dt;
    double vol_term = symbol.volatility * symbol.price * dW;
    
    symbol.price += drift_term + vol_term;
    
    // Bound prices to realistic range
    symbol.price = max(1.0, min(symbol.price, 100000.0));
}
```

---

## 3. Realism Considerations

### Bid-Ask Spread

The spread is proportional to price and random within bounds:

```
spread_pct = 0.05% + random(0, 0.15%)
half_spread = price * spread_pct / 2

bid_price = price - half_spread
ask_price = price + half_spread
```

This creates realistic spreads of 0.05% to 0.2% of the price.

### Trade vs Quote Distribution

Based on typical market activity:
- **70% Quote Updates**: Bid/ask changes without trades
- **30% Trade Executions**: Actual trades at market price

### Volume Generation

Trade quantities are randomly generated:
```
quantity = 100 + random(0, 9900)  // 100 to 10,000 shares
```

Quote quantities fluctuate around previous values:
```
new_qty = old_qty + random(-500, +500)
new_qty = max(100, new_qty)  // Minimum 100 shares
```

---

## 4. Statistical Properties

### Expected Behavior

Over time T, the price has:
- **Expected value**: E[S(T)] = S(0) · e^(μT)
- **Variance**: Var[S(T)] = S(0)² · e^(2μT) · (e^(σ²T) - 1)

### Sample Paths

With our parameters (μ=0, σ=0.02, dt=0.001):
- In 1 second (1000 ticks): Price changes ~±0.6%
- In 1 minute (60,000 ticks): Price changes ~±5%
- In 1 hour (360,000 ticks): Price changes ~±12%

### Correlation (Future Enhancement)

In real markets, volume and volatility are correlated. This could be added:
```cpp
if (abs(price_change) > 2 * avg_change) {
    volume *= 2;  // Higher volume on big moves
}
```
