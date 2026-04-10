permit(principal == MySQL::User::"alice_user", action == MySQL::Action::"SELECT", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"alice_user", action == MySQL::Action::"SELECT", resource == MySQL::Table::"company_db.employees");

permit(principal == MySQL::User::"bob_user", action == MySQL::Action::"SELECT", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"bob_user", action == MySQL::Action::"SELECT", resource == MySQL::Table::"company_db.projects");
permit(principal == MySQL::User::"bob_user", action == MySQL::Action::"INSERT", resource == MySQL::Table::"company_db.projects");
permit(principal == MySQL::User::"bob_user", action == MySQL::Action::"UPDATE", resource == MySQL::Table::"company_db.projects");

permit(principal == MySQL::User::"hr_manager", action == MySQL::Action::"SELECT", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"hr_manager", action == MySQL::Action::"SELECT", resource == MySQL::Table::"company_db.employees");
permit(principal == MySQL::User::"hr_manager", action == MySQL::Action::"SELECT", resource == MySQL::Table::"company_db.payroll");
permit(principal == MySQL::User::"hr_manager", action == MySQL::Action::"UPDATE", resource == MySQL::Table::"company_db.employees");

permit(principal == MySQL::User::"developer", action == MySQL::Action::"SELECT", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"developer", action == MySQL::Action::"SELECT", resource);

permit(principal == MySQL::User::"analyst", action == MySQL::Action::"SELECT", resource == MySQL::Database::"reporting_db");
permit(principal == MySQL::User::"analyst", action == MySQL::Action::"SELECT", resource == MySQL::Table::"reporting_db.public_reports");

permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"SELECT", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"CREATE", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"DROP", resource == MySQL::Database::"company_db");
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"SELECT", resource);
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"INSERT", resource);
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"UPDATE", resource);
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"DELETE", resource);
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"CREATE", resource);
permit(principal == MySQL::User::"admin_user", action == MySQL::Action::"DROP", resource);
