permit(principal == MySQL::User::"bob", action == MySQL::Action::"SELECT", resource == MySQL::Database::"test_cedar_db");
permit(principal == MySQL::User::"bob", action == MySQL::Action::"SELECT", resource == MySQL::Table::"test_cedar_db.employees");
permit(principal == MySQL::User::"bob", action == MySQL::Action::"SELECT", resource == MySQL::Column::"test_cedar_db.employees.id");

permit(principal == MySQL::User::"developer", action == MySQL::Action::"EXECUTE", resource == MySQL::Routine::"test_cedar_db.calculate_bonus");
