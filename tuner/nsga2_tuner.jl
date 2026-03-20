# ---------------------------------------------------------------------------
# nsga2_tuner.jl — NSGA-II multi-objective optimizer for CASCADE parameters
#
# Pure Julia implementation (no external packages required).
# Called from C++ via jluna. The C++ side registers `cpp_evaluate` which
# accepts a Vector{Float64} of parameters and returns a Vector{Float64}
# of objective values.
#
# Objectives (minimize all):
#   1. safety_risk    — false negative risk proxy (MUST be zero)
#   2. fuel_cost      — mean recovery burn Δv magnitude
#   3. compute_cost   — mean tick latency (ms)
#
# Hard constraint: any individual with safety_risk > 0 is dominated by all
# safe individuals (feasibility-first sorting à la Deb 2002).
# ---------------------------------------------------------------------------

# ===================================================================
# NSGA-II core (Deb et al. 2002)
# ===================================================================

"""
    fast_non_dominated_sort(objectives, feasible)

Returns a vector of fronts, where each front is a vector of indices (1-based).
`feasible[i]` is true if individual i satisfies all hard constraints.
Infeasible individuals are always dominated by feasible ones.
"""
function fast_non_dominated_sort(objectives::Matrix{Float64}, feasible::Vector{Bool})
    n = size(objectives, 1)   # number of individuals
    m = size(objectives, 2)   # number of objectives

    domination_count = zeros(Int, n)
    dominated_set = [Int[] for _ in 1:n]
    fronts = Vector{Vector{Int}}()

    for p in 1:n
        for q in 1:n
            p == q && continue
            if dominates(objectives, feasible, p, q)
                push!(dominated_set[p], q)
            elseif dominates(objectives, feasible, q, p)
                domination_count[p] += 1
            end
        end
    end

    # First front: individuals not dominated by anyone
    front1 = Int[]
    for i in 1:n
        if domination_count[i] == 0
            push!(front1, i)
        end
    end
    push!(fronts, front1)

    i = 1
    while !isempty(fronts[i])
        next_front = Int[]
        for p in fronts[i]
            for q in dominated_set[p]
                domination_count[q] -= 1
                if domination_count[q] == 0
                    push!(next_front, q)
                end
            end
        end
        i += 1
        push!(fronts, next_front)
    end

    # Remove last empty front
    if !isempty(fronts) && isempty(fronts[end])
        pop!(fronts)
    end

    return fronts
end

"""
    dominates(objectives, feasible, p, q)

Returns true if individual p dominates individual q.
Feasibility-first: feasible always dominates infeasible.
"""
function dominates(objectives::Matrix{Float64}, feasible::Vector{Bool}, p::Int, q::Int)
    # Feasibility-first
    if feasible[p] && !feasible[q]
        return true
    elseif !feasible[p] && feasible[q]
        return false
    end

    # Both same feasibility — use Pareto dominance (minimize all)
    m = size(objectives, 2)
    all_leq = true
    any_lt = false
    for j in 1:m
        if objectives[p, j] > objectives[q, j]
            all_leq = false
            break
        end
        if objectives[p, j] < objectives[q, j]
            any_lt = true
        end
    end
    return all_leq && any_lt
end

"""
    crowding_distance(objectives, front)

Compute crowding distance for individuals in a front.
"""
function crowding_distance(objectives::Matrix{Float64}, front::Vector{Int})
    n = length(front)
    if n <= 2
        return fill(Inf, n)
    end

    m = size(objectives, 2)
    dist = zeros(Float64, n)

    for j in 1:m
        # Sort front by objective j
        sorted_idx = sortperm([objectives[front[k], j] for k in 1:n])

        dist[sorted_idx[1]] = Inf
        dist[sorted_idx[end]] = Inf

        obj_min = objectives[front[sorted_idx[1]], j]
        obj_max = objectives[front[sorted_idx[end]], j]
        span = obj_max - obj_min

        if span < 1e-15
            continue
        end

        for k in 2:(n-1)
            dist[sorted_idx[k]] += (
                objectives[front[sorted_idx[k+1]], j] -
                objectives[front[sorted_idx[k-1]], j]
            ) / span
        end
    end

    return dist
end

"""
    sbx_crossover(p1, p2, bounds, eta=20.0)

Simulated binary crossover (SBX) with polynomial probability distribution.
"""
function sbx_crossover(p1::Vector{Float64}, p2::Vector{Float64},
                       lb::Vector{Float64}, ub::Vector{Float64};
                       eta::Float64=20.0)
    n = length(p1)
    c1 = copy(p1)
    c2 = copy(p2)

    for i in 1:n
        if rand() > 0.5
            continue
        end

        if abs(p1[i] - p2[i]) < 1e-14
            continue
        end

        y1 = min(p1[i], p2[i])
        y2 = max(p1[i], p2[i])
        delta = y2 - y1

        # Compute beta_q
        u = rand()
        if u <= 0.5
            beta_q = (2.0 * u)^(1.0 / (eta + 1.0))
        else
            beta_q = (1.0 / (2.0 * (1.0 - u)))^(1.0 / (eta + 1.0))
        end

        c1[i] = 0.5 * ((y1 + y2) - beta_q * delta)
        c2[i] = 0.5 * ((y1 + y2) + beta_q * delta)

        # Clamp to bounds
        c1[i] = clamp(c1[i], lb[i], ub[i])
        c2[i] = clamp(c2[i], lb[i], ub[i])
    end

    return c1, c2
end

"""
    polynomial_mutation(x, bounds, eta=20.0, prob=nothing)

Polynomial mutation operator.
"""
function polynomial_mutation!(x::Vector{Float64},
                              lb::Vector{Float64}, ub::Vector{Float64};
                              eta::Float64=20.0,
                              prob::Union{Nothing,Float64}=nothing)
    n = length(x)
    pm = isnothing(prob) ? 1.0 / n : prob

    for i in 1:n
        if rand() > pm
            continue
        end

        delta = ub[i] - lb[i]
        if delta < 1e-14
            continue
        end

        u = rand()
        if u < 0.5
            delta_q = (2.0 * u)^(1.0 / (eta + 1.0)) - 1.0
        else
            delta_q = 1.0 - (2.0 * (1.0 - u))^(1.0 / (eta + 1.0))
        end

        x[i] += delta_q * delta
        x[i] = clamp(x[i], lb[i], ub[i])
    end
end

"""
    tournament_select(rank, crowding, n)

Binary tournament selection based on rank and crowding distance.
"""
function tournament_select(rank::Vector{Int}, crowding::Vector{Float64})
    n = length(rank)
    a = rand(1:n)
    b = rand(1:n)
    while b == a && n > 1
        b = rand(1:n)
    end

    if rank[a] < rank[b]
        return a
    elseif rank[a] > rank[b]
        return b
    else
        return crowding[a] >= crowding[b] ? a : b
    end
end

# ===================================================================
# Parameter space definition
# ===================================================================

struct ParamDef
    name::String
    lb::Float64      # lower bound
    ub::Float64      # upper bound
    is_int::Bool      # integer parameter?
    default::Float64  # default value
end

function build_param_defs()
    defs = ParamDef[]

    # --- Broad Phase (7 params) ---
    push!(defs, ParamDef("broad_shell_margin_km",       20.0,  200.0, false, 50.0))
    push!(defs, ParamDef("broad_invalid_shell_pad_km", 100.0,  800.0, false, 200.0))
    push!(defs, ParamDef("broad_a_bin_width_km",       100.0, 1500.0, false, 500.0))
    push!(defs, ParamDef("broad_i_bin_width_deg",        5.0,   60.0, false, 20.0))
    push!(defs, ParamDef("broad_band_neighbor_bins",     1.0,    6.0, true,  2.0))
    push!(defs, ParamDef("broad_high_e_fail_open",       0.05,   0.5, false, 0.2))
    push!(defs, ParamDef("broad_dcriterion_threshold",   0.5,    5.0, false, 2.0))

    # --- Narrow Phase (17 params) ---
    push!(defs, ParamDef("narrow_tca_guard_km",          0.005,  0.10, false, 0.02))
    push!(defs, ParamDef("narrow_refine_band_km",        0.02,   0.50, false, 0.10))
    push!(defs, ParamDef("narrow_full_refine_band_km",   0.05,   1.00, false, 0.20))
    push!(defs, ParamDef("narrow_high_rel_speed_km_s",   2.0,   15.0, false, 8.0))
    push!(defs, ParamDef("narrow_high_rel_speed_extra_band_km", 0.02, 0.50, false, 0.10))
    push!(defs, ParamDef("narrow_full_refine_budget_base", 16.0, 256.0, true, 64.0))
    push!(defs, ParamDef("narrow_full_refine_budget_min",   2.0,  64.0, true,  8.0))
    push!(defs, ParamDef("narrow_full_refine_budget_max",  64.0, 512.0, true, 192.0))
    push!(defs, ParamDef("narrow_full_refine_samples",      4.0,  64.0, true, 16.0))
    push!(defs, ParamDef("narrow_full_refine_substep_s",   0.1,   5.0, false, 1.0))
    push!(defs, ParamDef("narrow_micro_refine_max_step_s", 1.0,  15.0, false, 5.0))
    push!(defs, ParamDef("narrow_plane_angle_threshold_deg", 30.0, 150.0, false, 75.0))
    push!(defs, ParamDef("narrow_phase_angle_threshold_deg", 90.0, 179.0, false, 150.0))
    push!(defs, ParamDef("narrow_phase_max_e",             0.05,   0.5, false, 0.2))
    push!(defs, ParamDef("narrow_moid_samples",           12.0, 144.0, true, 72.0))
    push!(defs, ParamDef("narrow_moid_reject_threshold_km", 0.5, 10.0, false, 2.0))
    push!(defs, ParamDef("narrow_moid_max_e",              0.05,  0.5, false, 0.2))

    # --- Recovery Planner (7 params) ---
    push!(defs, ParamDef("recovery_scale_t",           1e-6,   1e-3, false, 6e-5))
    push!(defs, ParamDef("recovery_scale_r",           1e-4,   1e-1, false, 2e-3))
    push!(defs, ParamDef("recovery_radial_share",       0.1,    0.9, false, 0.5))
    push!(defs, ParamDef("recovery_scale_n",           1e-4,   1e-1, false, 6e-3))
    push!(defs, ParamDef("recovery_fallback_norm_km_s", 1e-6,   1e-2, false, 1e-4))
    push!(defs, ParamDef("recovery_max_request_ratio",  0.01,   0.20, false, 0.05))
    push!(defs, ParamDef("recovery_solver_mode",        0.0,    1.0, true,  1.0))  # 0=HEURISTIC, 1=CW

    # --- Propagator fast-lane (9 params) ---
    push!(defs, ParamDef("fast_lane_max_dt_s",         10.0,   60.0, false, 30.0))
    push!(defs, ParamDef("fast_lane_max_e",            0.005,  0.05, false, 0.02))
    push!(defs, ParamDef("fast_lane_min_perigee_alt_km", 200.0, 800.0, false, 500.0))
    push!(defs, ParamDef("fast_lane_ext_max_dt_s",     20.0,   90.0, false, 45.0))
    push!(defs, ParamDef("fast_lane_ext_max_e",        0.001,  0.01, false, 0.003))
    push!(defs, ParamDef("fast_lane_ext_min_perigee_alt_km", 400.0, 900.0, false, 650.0))
    push!(defs, ParamDef("probe_max_step_s",           30.0,  300.0, false, 120.0))
    push!(defs, ParamDef("probe_pos_thresh_km",        0.1,    2.0, false, 0.5))
    push!(defs, ParamDef("probe_vel_thresh_ms",        0.1,    2.0, false, 0.5))

    # --- CDM scanner (3 params) ---
    push!(defs, ParamDef("cdm_horizon_s",           43200.0, 172800.0, false, 86400.0))
    push!(defs, ParamDef("cdm_substep_s",             120.0,   1800.0, false, 600.0))
    push!(defs, ParamDef("cdm_rk4_max_step_s",        10.0,     60.0, false, 30.0))

    # --- COLA auto-impulse (1 param) ---
    push!(defs, ParamDef("cola_auto_dv_km_s",       0.0001,    0.01, false, 0.001))

    # --- CW solver bounds (7 params) ---
    push!(defs, ParamDef("cw_horizon_fraction",        0.05,    0.5, false, 0.25))
    push!(defs, ParamDef("cw_horizon_min_s",           60.0,   600.0, false, 300.0))
    push!(defs, ParamDef("cw_horizon_max_s",         1800.0, 10800.0, false, 5400.0))
    push!(defs, ParamDef("cw_pos_blend",               0.3,    1.0, false, 0.7))
    push!(defs, ParamDef("cw_vel_blend",               0.0,    0.7, false, 0.3))
    push!(defs, ParamDef("cw_rem_error_cap",           0.05,   0.5, false, 0.15))
    push!(defs, ParamDef("cw_heur_norm_cap",           0.1,    0.8, false, 0.4))

    # --- Slot error normalization (4 params) ---
    push!(defs, ParamDef("slot_norm_a_km",             1.0,   50.0, false, 10.0))
    push!(defs, ParamDef("slot_norm_e",              1e-4,    1e-2, false, 1e-3))
    push!(defs, ParamDef("slot_norm_i_rad",          1e-4,    1e-2, false, 1e-3))
    push!(defs, ParamDef("slot_norm_raan_rad",       1e-4,    1e-2, false, 1e-3))

    # --- Operational constants (5 params) ---
    push!(defs, ParamDef("signal_latency_s",           1.0,   30.0, false, 10.0))
    push!(defs, ParamDef("stationkeeping_box_radius_km", 2.0, 30.0, false, 10.0))
    push!(defs, ParamDef("upload_scan_step_s",         5.0,   60.0, false, 20.0))
    push!(defs, ParamDef("auto_upload_horizon_s",    600.0,  3600.0, false, 1800.0))
    push!(defs, ParamDef("graveyard_target_dv_km_s", 0.001,   0.01, false, 0.003))

    # --- Budget tier params (9 params) ---
    push!(defs, ParamDef("budget_tier1_threshold",  500000.0, 5000000.0, true, 2000000.0))
    push!(defs, ParamDef("budget_tier1_budget",         5.0,     100.0, true,  20.0))
    push!(defs, ParamDef("budget_tier2_threshold",  100000.0, 2000000.0, true, 500000.0))
    push!(defs, ParamDef("budget_tier2_budget",        10.0,     128.0, true,  32.0))
    push!(defs, ParamDef("budget_tier3_threshold",  20000.0,   500000.0, true, 100000.0))
    push!(defs, ParamDef("budget_tier3_budget",        20.0,     256.0, true,  96.0))
    push!(defs, ParamDef("budget_tier4_threshold",  50000.0,  1000000.0, true, 300000.0))
    push!(defs, ParamDef("budget_tier4_budget",        20.0,     256.0, true,  80.0))
    push!(defs, ParamDef("budget_short_step_bonus",     5.0,     100.0, true,  24.0))

    return defs
end

# ===================================================================
# Main NSGA-II driver
# ===================================================================

"""
    run_nsga2(evaluate_fn; pop_size, n_generations, seed)

Run NSGA-II optimization. `evaluate_fn(x::Vector{Float64})` must return
a `Vector{Float64}` of objective values (all minimized).
The first objective is the safety risk — individuals with risk > 0 are
treated as infeasible.
"""
function run_nsga2(evaluate_fn::Function;
                   pop_size::Int=80,
                   n_generations::Int=50,
                   seed::Int=20260320)

    Random.seed!(seed)

    defs = build_param_defs()
    n_params = length(defs)
    lb = [d.lb for d in defs]
    ub = [d.ub for d in defs]

    println("[nsga2] parameters: $n_params")
    println("[nsga2] population: $pop_size, generations: $n_generations")

    # --- Initialize population ---
    population = Matrix{Float64}(undef, pop_size, n_params)
    for i in 1:pop_size
        for j in 1:n_params
            population[i, j] = lb[j] + rand() * (ub[j] - lb[j])
            if defs[j].is_int
                population[i, j] = round(population[i, j])
            end
        end
    end

    # Include default as first individual
    for j in 1:n_params
        population[1, j] = defs[j].default
    end

    # Evaluate initial population
    n_obj = 0
    objectives = nothing
    feasible = Vector{Bool}(undef, pop_size)

    println("[nsga2] evaluating initial population...")
    for i in 1:pop_size
        x = population[i, :]
        obj = evaluate_fn(x)
        if n_obj == 0
            n_obj = length(obj)
            objectives = Matrix{Float64}(undef, pop_size, n_obj)
        end
        objectives[i, :] .= obj
        feasible[i] = (obj[1] <= 0.0)  # first objective = safety risk
        if i % 10 == 0
            println("[nsga2] evaluated $i/$pop_size")
        end
    end

    # --- Generational loop ---
    for gen in 1:n_generations
        # Compute ranks and crowding for current population
        fronts = fast_non_dominated_sort(objectives, feasible)
        rank = zeros(Int, pop_size)
        crowd = zeros(Float64, pop_size)

        for (r, front) in enumerate(fronts)
            cd = crowding_distance(objectives, front)
            for (k, idx) in enumerate(front)
                rank[idx] = r
                crowd[idx] = cd[k]
            end
        end

        # Generate offspring
        offspring_pop = Matrix{Float64}(undef, pop_size, n_params)
        for i in 1:2:pop_size
            p1 = tournament_select(rank, crowd)
            p2 = tournament_select(rank, crowd)

            c1, c2 = sbx_crossover(population[p1, :], population[p2, :], lb, ub)
            polynomial_mutation!(c1, lb, ub)
            polynomial_mutation!(c2, lb, ub)

            # Round integer params
            for j in 1:n_params
                if defs[j].is_int
                    c1[j] = round(c1[j])
                    c2[j] = round(c2[j])
                end
            end

            offspring_pop[i, :] .= c1
            if i + 1 <= pop_size
                offspring_pop[i + 1, :] .= c2
            end
        end

        # Evaluate offspring
        offspring_obj = Matrix{Float64}(undef, pop_size, n_obj)
        offspring_feasible = Vector{Bool}(undef, pop_size)

        for i in 1:pop_size
            x = offspring_pop[i, :]
            obj = evaluate_fn(x)
            offspring_obj[i, :] .= obj
            offspring_feasible[i] = (obj[1] <= 0.0)
        end

        # Merge parent + offspring
        merged_pop = vcat(population, offspring_pop)
        merged_obj = vcat(objectives, offspring_obj)
        merged_feasible = vcat(feasible, offspring_feasible)

        # Non-dominated sort on merged population
        merged_fronts = fast_non_dominated_sort(merged_obj, merged_feasible)

        # Select next generation (elitist)
        new_pop = Matrix{Float64}(undef, pop_size, n_params)
        new_obj = Matrix{Float64}(undef, pop_size, n_obj)
        new_feasible = Vector{Bool}(undef, pop_size)
        filled = 0

        for front in merged_fronts
            if filled >= pop_size
                break
            end

            cd = crowding_distance(merged_obj, front)

            if filled + length(front) <= pop_size
                # Entire front fits
                for (k, idx) in enumerate(front)
                    filled += 1
                    new_pop[filled, :] .= merged_pop[idx, :]
                    new_obj[filled, :] .= merged_obj[idx, :]
                    new_feasible[filled] = merged_feasible[idx]
                end
            else
                # Partial front — select by crowding distance
                sorted_by_cd = sortperm(cd, rev=true)
                remaining = pop_size - filled
                for k in 1:remaining
                    idx = front[sorted_by_cd[k]]
                    filled += 1
                    new_pop[filled, :] .= merged_pop[idx, :]
                    new_obj[filled, :] .= merged_obj[idx, :]
                    new_feasible[filled] = merged_feasible[idx]
                end
            end
        end

        population = new_pop
        objectives = new_obj
        feasible = new_feasible

        # Report progress
        n_safe = count(feasible)
        best_compute = Inf
        best_fuel = Inf
        for i in 1:pop_size
            if feasible[i]
                best_compute = min(best_compute, objectives[i, 3])
                best_fuel = min(best_fuel, objectives[i, 2])
            end
        end

        println("[nsga2] gen $gen/$n_generations: safe=$n_safe/$pop_size " *
                "best_fuel=$(round(best_fuel, digits=6)) " *
                "best_compute=$(round(best_compute, digits=3))")
    end

    # --- Extract Pareto front ---
    final_fronts = fast_non_dominated_sort(objectives, feasible)
    pareto_indices = final_fronts[1]

    # Filter to feasible-only Pareto front
    safe_pareto = filter(i -> feasible[i], pareto_indices)

    println("\n[nsga2] === RESULTS ===")
    println("[nsga2] Pareto front size: $(length(safe_pareto))")

    # Sort by compute cost
    sort!(safe_pareto, by=i -> objectives[i, 3])

    # Return results as a dict of arrays
    result = Dict{String, Any}(
        "pareto_indices" => safe_pareto,
        "population" => population,
        "objectives" => objectives,
        "feasible" => feasible,
        "param_names" => [d.name for d in defs],
        "param_defaults" => [d.default for d in defs],
    )

    # Print top-10 Pareto solutions
    n_report = min(10, length(safe_pareto))
    for k in 1:n_report
        idx = safe_pareto[k]
        println("[nsga2] candidate[$k]: safety=$(objectives[idx,1]) " *
                "fuel=$(round(objectives[idx,2], digits=6)) " *
                "compute=$(round(objectives[idx,3], digits=3)) ms")
    end

    return result
end

# Import Random for seed
using Random
