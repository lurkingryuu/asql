// Sample Cedar Policies for MySQL Authorization Plugin
// These policies demonstrate various authorization scenarios

// Policy 1: Basic user access to specific table
permit (
    principal == User::"alice",
    action == Action::"Select",
    resource == Table::"employees"
);

// Policy 2: Time-based access (business hours only)
permit (
    principal == User::"bob",
    action in [Action::"Select", Action::"Insert", Action::"Update"],
    resource == Table::"orders"
) when {
    context.time >= 90000 && context.time <= 170000  // 9:00 AM to 5:00 PM
};

// Policy 3: IP-based restrictions (internal network only)
permit (
    principal == User::"charlie",
    action == Action::"Select",
    resource
) when {
    ip("192.168.1.0/24").contains(context.ip) ||
    ip("10.0.0.0/8").contains(context.ip)
};

// Policy 4: Weekend maintenance access
permit (
    principal == User::"maintenance",
    action in [Action::"Create", Action::"Drop", Action::"Alter"],
    resource
) when {
    context.day == "sat" || context.day == "sun"
};

// Policy 5: Database admin with full access during weekdays
permit (
    principal == User::"admin",
    action,
    resource
) when {
    context.day in ["mon", "tue", "wed", "thu", "fri"]
};

// Policy 6: Read-only access for analysts
permit (
    principal in Role::"analysts",
    action == Action::"Select",
    resource
) when {
    resource.type == "Table"
};

// Policy 7: Deny access to sensitive tables outside business hours
forbid (
    principal,
    action,
    resource == Table::"payroll"
) when {
    context.time < 80000 || context.time > 180000  // Before 8:00 AM or after 6:00 PM
};

// Policy 8: Emergency access override
permit (
    principal == User::"emergency",
    action,
    resource
) when {
    context.has("emergency_mode") && context.emergency_mode == true
};

// Policy 9: Restrict DELETE operations to specific users
permit (
    principal in [User::"alice", User::"admin"],
    action == Action::"Delete",
    resource
);

// Policy 10: Column-level access control
permit (
    principal == User::"hr_user",
    action == Action::"Select",
    resource == Column::"salary"
) when {
    context.time >= 90000 && context.time <= 170000 &&
    ip("192.168.100.0/24").contains(context.ip)
};

// Policy 11: Procedure execution restrictions
permit (
    principal == User::"developer",
    action == Action::"Execute",
    resource == Routine::"calculate_bonus"
) when {
    context.day in ["mon", "tue", "wed", "thu", "fri"] &&
    context.time >= 90000 && context.time <= 170000
};

// Policy 12: Database creation only for DBAs
permit (
    principal in Role::"dba",
    action == Action::"Create",
    resource
) when {
    resource.type == "Database"
};

// Policy 13: Temporary access with date restrictions
permit (
    principal == User::"temp_contractor",
    action == Action::"Select",
    resource == Table::"public_data"
) when {
    context.date >= 20250101 && context.date <= 20250331  // Q1 2025 only
};

// Policy 14: Cross-reference with external data
permit (
    principal,
    action == Action::"Select",
    resource
) when {
    context.has("user_clearance_level") &&
    context.user_clearance_level >= 3
};

// Policy 15: Audit user with read-only access to all tables
permit (
    principal == User::"auditor",
    action == Action::"Select",
    resource
) when {
    resource.type in ["Table", "Database", "Column"]
};
