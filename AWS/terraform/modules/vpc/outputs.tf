variable "name_prefix" {}
variable "environment" {}

output "vpc_id"              { value = aws_vpc.main.id }
output "public_subnet_ids"   { value = aws_subnet.public[*].id }
output "private_subnet_ids"  { value = aws_subnet.private[*].id }
output "database_subnet_ids" { value = aws_subnet.database[*].id }
output "sg_alb_id"           { value = aws_security_group.alb.id }
output "sg_ecs_id"           { value = aws_security_group.ecs_tasks.id }
output "sg_rds_id"           { value = aws_security_group.rds.id }
output "sg_redis_id"         { value = aws_security_group.redis.id }
output "sg_gamelift_id"      { value = aws_security_group.gamelift.id }
