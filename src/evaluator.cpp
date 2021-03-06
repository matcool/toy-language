#include "evaluator.hpp"
#include "enums.hpp"

int Evaluator::run() {
	for (auto& function : m_parser.m_functions) {
		if (function.name == "main") {
			const auto value = eval_function(function, {});
			assert(std::holds_alternative<int>(value.data), format("oh cmon {}", value.data.index()));
			return std::get<int>(value.data);
		}
	}
	assert(false, "main not found");
	return -1;
}

Evaluator::Value Evaluator::eval_function(Function& function, std::vector<Value> args) {
	Scope scope;
	assert(function.arguments.size() == args.size(), "function args mismatch");
	for (size_t i = 0; i < args.size(); ++i) {
		scope.add_variable(function.arguments[i].name, std::move(args[i]));
	}
	args.clear();
	for (auto& stmt : function.statements) {
		const auto result = eval_statement(stmt, function, scope);
		if (result) return *result;
	}
	unhandled("No return statement was reached.. implement implicit return for void");
}

std::optional<Evaluator::Value> Evaluator::eval_statement(Statement& stmt, Function& parent, Scope& scope) {
	if (stmt.type == StatementType::Return) {
		return eval_expression(stmt.expressions[0], parent, scope);
	} else if (stmt.type == StatementType::Expression) {
		eval_expression(stmt.expressions[0], parent, scope);
	} else if (stmt.type == StatementType::If) {
		const auto value = eval_expression(stmt.expressions[0], parent, scope);
		if (std::get<bool>(value.data)) {
			// TODO: avoid duplicating this code
			for (auto& stmt : stmt.children) {
				const auto result = eval_statement(stmt, parent, scope);
				if (result) return *result;
			}
		} else if (stmt.else_branch) {
			// here
			const auto result = eval_statement(*stmt.else_branch, parent, scope);
			if (result) return *result;
		}
	} else if (stmt.type == StatementType::Else) {
		// and here
		for (auto& stmt : stmt.children) {
			const auto result = eval_statement(stmt, parent, scope);
			if (result) return *result;
		}
	} else if (stmt.type == StatementType::While) {
		while (true) {
			const auto value = eval_expression(stmt.expressions[0], parent, scope);
			if (!std::get<bool>(value.data)) break;
			// and here
			for (auto& stmt : stmt.children) {
				const auto result = eval_statement(stmt, parent, scope);
				if (result) return *result;
			}
		}
	} else {
		assert(false, format("Unhandled statement: {}", enum_name(stmt.type)));
	}
	return std::nullopt;
}

Evaluator::Value Evaluator::eval_expression(Expression& expression, Function& parent, Scope& scope) {
	return expression.match(
		[&](const Expression::LiteralData& data) {
			return std::visit([&](auto value) {
				return Value { expression.value_type, value };
			}, data.value);
		},
		[&](const Expression::OperatorData& data) {
			// TODO: OperatorKind or smth, at least have some way to separate into binary and unary
			auto lhs = eval_expression(expression.children[0], parent, scope);
			auto rhs = eval_expression(expression.children[1], parent, scope);
			if (data.op_type == OperatorType::Addition) {
				if (expression.value_type.name == "i32") {
					return Value {
						expression.value_type, std::get<int>(lhs.data) + std::get<int>(rhs.data)
					};
				}
			} else if (data.op_type == OperatorType::Equals) {
				return Value {
					expression.value_type,
					std::visit([&](const auto& a, const auto& b) {
						if constexpr (requires { a == b; }) {
							return a == b;
						} else {
							assert(false, "Unhandled comparison");
							return false;
						}
					}, lhs.data, rhs.data)
				};
			} else if (data.op_type == OperatorType::NotEquals) {
				return Value {
					expression.value_type,
					std::visit([&](const auto& a, const auto& b) {
						if constexpr (requires { a != b; }) {
							return a != b;
						} else {
							assert(false, "Unhandled comparison");
							return false;
						}
					}, lhs.data, rhs.data)
				};
			}
			assert(false, format("Unhandled operator {}", enum_name(data.op_type)));
			std::abort();
		},
		[&](const Expression::DeclarationData& data) {
			Value& value = scope.add_variable(data.var.name, Value { data.var.type });
			return Value { data.var.type.add_reference(), std::ref(value) };
		},
		[&](const Expression::VariableData& data) {
			auto result = scope.get_variable(data.name);
			if (!result.has_value()) {
				assert(false, "variable not found in evaluator, should not happen");
			}
			Value& value = result.value();
			return Value { value.type.add_reference(), std::ref(value) };
		},
		[&](MatchValue<ExpressionType::Assignment>) {
			auto rhs = eval_expression(expression.children[1], parent, scope);
			auto lhs = eval_expression(expression.children[0], parent, scope);
			Value& value = std::get<std::reference_wrapper<Value>>(lhs.data);
			value.data = rhs.data;
			return lhs;
		},
		[&](const Expression::CallData& data) {
			std::vector<Value> values;
			for (auto& child : expression.children) {
				values.emplace_back(eval_expression(child, parent, scope));
			}
			auto& function = m_parser.function_by_name(data.function_name);
			return eval_function(function, values);
		},
		[&](MatchValue<ExpressionType::Cast>) {
			auto value = eval_expression(expression.children[0], parent, scope);
			const auto& child_type = expression.children[0].value_type;
			if (!expression.value_type.unref_eq(child_type)) {
				assert(false, format("dont know how to convert {} to {}", child_type, expression.value_type));
			}
			if (child_type.reference && !expression.value_type.reference) {
				return std::get<std::reference_wrapper<Value>>(value.data).get();
			}
		},
		[&](auto) {
			assert(false, format("unhandled expression: {}", enum_name(expression.type)));
			return Value{};
		}
	);

}