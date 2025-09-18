#!/usr/bin/env python3
"""
Mock Cedar Authorization Service for Testing

This is a simple mock service that simulates a Cedar authorization service
for testing the Cedar MySQL authorization plugin. It implements basic
rules to demonstrate different authorization scenarios.

Usage:
    python3 mock_cedar_service.py [--port PORT] [--host HOST]

Default: http://localhost:8180
"""

import json
import argparse
from datetime import datetime
from flask import Flask, request, jsonify
import ipaddress

app = Flask(__name__)

# Mock authorization rules
# In a real Cedar service, these would be proper Cedar policies
MOCK_RULES = {
    # Basic user access
    # Alice allowed to SELECT (broad allow to accommodate column-level events)
    "alice_select_allow": {
        "principal": "User::\"alice\"",
        "action": "Action::\"Select\"",
        "decision": "Allow"
    },
    
    # Time-based access (9 AM to 5 PM)
    "bob_orders_business_hours": {
        "principal": "User::\"bob\"",
        "actions": ["Action::\"Select\"", "Action::\"Insert\"", "Action::\"Update\""],
        "resource": "Table::\"orders\"",
        "time_range": (90000, 170000),
        "decision": "Allow"
    },
    
    # IP-based restrictions
    "charlie_internal_network": {
        "principal": "User::\"charlie\"",
        "action": "Action::\"Select\"",
        "ip_ranges": ["192.168.1.0/24", "10.0.0.0/8"],
        "decision": "Allow"
    },
    
    # Weekend maintenance
    "maintenance_weekend": {
        "principal": "User::\"maintenance\"",
        "actions": ["Action::\"Create\"", "Action::\"Drop\"", "Action::\"Alter\""],
        "days": ["sat", "sun"],
        "decision": "Allow"
    },
    
    # Admin weekday access
    "admin_weekdays": {
        "principal": "User::\"admin\"",
        "days": ["mon", "tue", "wed", "thu", "fri"],
        "decision": "Allow"  # All actions
    },
    
    # Payroll access restrictions (deny outside 8 AM - 6 PM)
    "payroll_business_hours": {
        "resource": "Table::\"payroll\"",
        "time_range": (80000, 180000),
        "decision": "Deny"  # Deny outside business hours
    },
    
    # Delete restrictions
    "delete_restricted": {
        "action": "Action::\"Delete\"",
        "allowed_users": ["User::\"alice\"", "User::\"admin\""],
        "decision": "Allow"
    },
    
    # Auditor read access
    "auditor_read_all": {
        "principal": "User::\"auditor\"",
        "action": "Action::\"Select\"",
        "decision": "Allow"
    }
}

def check_ip_in_ranges(client_ip, ip_ranges):
    """Check if client IP is in any of the allowed ranges"""
    try:
        client_addr = ipaddress.ip_address(client_ip)
        for ip_range in ip_ranges:
            if client_addr in ipaddress.ip_network(ip_range, strict=False):
                return True
    except ValueError:
        pass
    return False

def evaluate_request(principal, action, resource, context, privileges=None):
    """Evaluate authorization request against mock rules"""

    # Extract context information
    day = context.get("day", "")
    time = context.get("time", 0)
    client_ip = "unknown"

    # Extract IP from Cedar extension format
    if "ip" in context and "__extn" in context["ip"]:
        client_ip = context["ip"]["__extn"].get("arg", "unknown")

    print(f"Evaluating: {principal} -> {action} on {resource}")
    print(f"Context: day={day}, time={time}, ip={client_ip}")
    if privileges:
        print(f"Privileges: {privileges}")

    # Convert privileges to actions for evaluation
    actions_to_check = []
    if privileges:
        # Map privileges to actions (simplified mapping)
        priv_mapping = {
            "SELECT": "Select",
            "INSERT": "Insert",
            "UPDATE": "Update",
            "DELETE": "Delete",
            "CREATE": "Create",
            "DROP": "Drop",
            "ALTER": "Alter",
            "EXECUTE": "Execute"
        }
        for priv in privileges:
            if priv in priv_mapping:
                actions_to_check.append(f"Action::\"{priv_mapping[priv]}\"")
    else:
        actions_to_check = [action]

    print(f"Actions to check: {actions_to_check}")

    # Deny-override precheck: Payroll table SELECT outside business hours
    if resource == "Table::\"payroll\"" and "Action::\"Select\"" in actions_to_check:
        if not (80000 <= time <= 180000):
            return "Deny", "Payroll access denied outside 8 AM - 6 PM"

    # Evaluate each action (ANY-of semantics: authorize if any action is allowed)
    allowed_any = False
    last_deny_reason = "No matching policy found"
    for action_to_check in actions_to_check:
        print(f"Checking action: {action_to_check}")

        # Rule 1: Alice SELECT allow (table/column agnostic)
        if (principal == "User::\"alice\"" and
            action_to_check == "Action::\"Select\""):
            allowed_any = True
            continue

        # Rule 2: Bob's time-based access to orders
        if (principal == "User::\"bob\"" and
            action_to_check in ["Action::\"Select\"", "Action::\"Insert\"", "Action::\"Update\""] and
            resource == "Table::\"orders\""):
            if 90000 <= time <= 170000:
                allowed_any = True
                continue
            else:
                last_deny_reason = "Outside business hours (9 AM - 5 PM)"

        # Rule 3: Charlie's IP-based access
        if (principal == "User::\"charlie\"" and
            action_to_check == "Action::\"Select\""):
            ip_ranges = ["192.168.1.0/24", "10.0.0.0/8"]
            if check_ip_in_ranges(client_ip, ip_ranges):
                allowed_any = True
                continue
            else:
                last_deny_reason = f"IP {client_ip} not in allowed ranges"

        # Rule 4: Maintenance weekend access
        if (principal == "User::\"maintenance\"" and
            action_to_check in ["Action::\"Create\"", "Action::\"Drop\"", "Action::\"Alter\""]):
            if day in ["sat", "sun"]:
                allowed_any = True
                continue
            else:
                last_deny_reason = "Maintenance only allowed on weekends"

        # Rule 5: Admin weekday access
        if principal == "User::\"admin\"":
            if day in ["mon", "tue", "wed", "thu", "fri"]:
                allowed_any = True
                continue
            else:
                last_deny_reason = "Admin access restricted to weekdays"

        # Rule 6: Payroll access restrictions
        if (resource == "Table::\"payroll\"" and
            action_to_check == "Action::\"Select\""):
            if not (80000 <= time <= 180000):
                last_deny_reason = "Payroll access denied outside 8 AM - 6 PM"
            else:
                allowed_any = True
                continue

        # Rule 7: Delete operation restrictions
        if action_to_check == "Action::\"Delete\"":
            if principal in ["User::\"alice\"", "User::\"admin\""]:
                allowed_any = True
                continue
            else:
                last_deny_reason = "Delete operations restricted to specific users"

        # Rule 8: Auditor read access
        if (principal == "User::\"auditor\"" and
            action_to_check == "Action::\"Select\""):
            allowed_any = True
            continue

        # Rule 9: HR user salary column access
        if (principal == "User::\"hr_user\"" and
            action_to_check == "Action::\"Select\"" and
            resource == "Column::\"salary\""):
            if (90000 <= time <= 170000 and
                check_ip_in_ranges(client_ip, ["192.168.100.0/24"])):
                allowed_any = True
                continue
            else:
                last_deny_reason = "HR salary access restricted to business hours and internal network"

        # Rule 10: Developer procedure execution
        if (principal == "User::\"developer\"" and
            action_to_check == "Action::\"Execute\"" and
            resource == "Routine::\"calculate_bonus\""):
            if (day in ["mon", "tue", "wed", "thu", "fri"] and
                90000 <= time <= 170000):
                allowed_any = True
                continue
            else:
                last_deny_reason = "Procedure execution restricted to weekday business hours"

    # Final decision
    if allowed_any:
        return "Allow", "One or more requested actions authorized"
    else:
        return "Deny", last_deny_reason

@app.route('/v1/is_authorized', methods=['POST'])
def is_authorized():
    """Cedar authorization endpoint"""
    try:
        # Parse request
        data = request.get_json()
        if not data:
            return jsonify({
                "decision": "Deny",
                "diagnostics": {
                    "errors": ["Invalid JSON request"]
                }
            }), 400

        principal = data.get("principal", "")
        action = data.get("action", "")
        resource = data.get("resource", "")
        context = data.get("context", {})
        privileges = data.get("privileges", [])

        # Log the request
        print(f"\n--- Cedar Authorization Request ---")
        print(f"Principal: {principal}")
        print(f"Action: {action}")
        print(f"Resource: {resource}")
        print(f"Privileges: {privileges}")
        print(f"Context: {json.dumps(context, indent=2)}")

        # Evaluate request
        decision, reason = evaluate_request(principal, action, resource, context, privileges)

        response = {
            "decision": decision,
            "diagnostics": {
                "errors": [] if decision == "Allow" else [f"Access denied: {reason}"],
                "reason": reason
            }
        }

        print(f"Decision: {decision}")
        print(f"Reason: {reason}")
        print("--- End Request ---\n")

        return jsonify(response)

    except Exception as e:
        print(f"Error processing request: {str(e)}")
        return jsonify({
            "decision": "Deny",
            "diagnostics": {
                "errors": [f"Internal error: {str(e)}"]
            }
        }), 500

@app.route('/health', methods=['GET'])
def health_check():
    """Health check endpoint"""
    return jsonify({
        "status": "healthy",
        "service": "Mock Cedar Authorization Service",
        "timestamp": datetime.now().isoformat()
    })

@app.route('/', methods=['GET'])
def info():
    """Service information"""
    return jsonify({
        "service": "Mock Cedar Authorization Service",
        "version": "1.0.0",
        "endpoints": {
            "/v1/is_authorized": "POST - Authorization endpoint",
            "/health": "GET - Health check",
            "/": "GET - Service info"
        },
        "mock_rules": len(MOCK_RULES)
    })

def main():
    parser = argparse.ArgumentParser(description='Mock Cedar Authorization Service')
    parser.add_argument('--host', default='localhost', help='Host to bind to')
    parser.add_argument('--port', type=int, default=8180, help='Port to bind to')
    parser.add_argument('--debug', action='store_true', help='Enable debug mode')
    
    args = parser.parse_args()
    
    print(f"Starting Mock Cedar Authorization Service...")
    print(f"URL: http://{args.host}:{args.port}")
    print(f"Authorization endpoint: http://{args.host}:{args.port}/v1/is_authorized")
    print(f"Health check: http://{args.host}:{args.port}/health")
    print(f"Debug mode: {args.debug}")
    print(f"Mock rules loaded: {len(MOCK_RULES)}")
    print("\nReady to receive authorization requests...\n")
    
    app.run(host=args.host, port=args.port, debug=args.debug)

if __name__ == '__main__':
    main()
