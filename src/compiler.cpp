#include "compiler.hpp"
#include "enums.hpp"
#include "format.hpp"
#include <array>

void Compiler::compile() {
	for (auto& function : m_parser.m_functions) {
		compile_function(function);
	}
	write("section .data");
	for (size_t i = 0; i < m_strings.size(); ++i) {
		write("data_{}: db \"{}\"", i, m_strings[i]);
	}
}

void Compiler::compile_function(Function& function) {
	if (function.builtin) {
		if (function.name == "print") {
			write(R"(
print:
mov eax, [esp + 4] ; number

mov ecx, esp ; buffer ptr
sub esp, 12  ; buffer of 12 bytes
mov ebx, 0   ; buffer count

print_loop:
sub ecx, 1 ; move pointer to where we're writing
; idiv
mov esi, 10 ; divide by 10
mov edx, 0 ; clear edx for idiv
idiv esi ; divide edx|eax by 10, result -> eax, modulo -> edx
add edx, '0' ; offset by ascii '0'
mov BYTE [ecx], dl ; write edx (lower byte) into the buffer
add ebx, 1 ; increase buffer count

cmp eax, 0
jg print_loop ; jump if greater than 0

mov eax, 4   ; WRITE syscall
mov edx, ebx ; buf size
mov ebx, 1   ; fd 1 (stdout)
; ecx          buf*
int 0x80 ; syscall

mov eax, 4   ; WRITE syscall
mov BYTE [ecx], 10 ; newline
mov edx, 1   ; buf size
int 0x80 ; syscall

add esp, 12 ; revert buffer

ret)");
		} else {
			assert(false, format("unknown builtin {}", function.name));
		}
		return;
	}

	write("{}:", function.name);
	m_cur_function = &function;
	m_var_counter = 0;
	m_label_counter = 0;
	m_variables.clear();
	for (size_t i = 0; i < function.arguments.size(); ++i) {
		m_variables[function.arguments[i].name] = -(function.arguments.size() - i - 1 + 2);
	}
	if (!function.scope.variables.empty() || !function.arguments.empty()) {
		write("push ebp");
		write("mov ebp, esp");
		if (!function.scope.variables.empty())
			write("sub esp, {}", function.scope.variables.size() * 4);
	}
	for (auto& statement : function.statements) {
		compile_statement(statement);
	}
	generate_return(function);
	m_cur_function = nullptr;
	write("");
}

void Compiler::compile_statement(Statement& statement) {
	if (statement.type == StatementType::Return) {
		if (!statement.expressions.empty()) {
			// output should be in eax
			compile_expression(statement.expressions[0]);
		}
		// shouldnt ever be null
		if (m_cur_function != nullptr)
			generate_return(*m_cur_function);
	} else if (statement.type == StatementType::Expression) {
		compile_expression(statement.expressions[0]);
	} else if (statement.type == StatementType::If) {
		compile_expression(statement.expressions[0]);
		const auto end_label = format("{}_if_end_{}", m_cur_function->name, m_label_counter++);
		const auto else_label = format("{}_if_else_{}", m_cur_function->name, m_label_counter++);
		write("cmp al, 1");
		write("jne {}", statement.else_branch ? else_label : end_label);
		for (auto& child : statement.children) {
			compile_statement(child);
		}
		if (statement.else_branch) {
			write("jmp {}", end_label);
			write("{}:", else_label);
			compile_statement(*statement.else_branch);
		}
		write("{}:", end_label);
	} else if (statement.type == StatementType::Else) {
		// TODO: Else is basically just a block statement, maybe rename it?
		for (auto& child : statement.children) {
			compile_statement(child);
		}
	} else if (statement.type == StatementType::While) {
		const auto label_start = format("{}_while_start_{}", m_cur_function->name, m_label_counter++);
		const auto label_end = format("{}_while_end_{}", m_cur_function->name, m_label_counter++);
		write("{}:", label_start);
		compile_expression(statement.expressions[0]);
		write("cmp al, 1");
		write("jne {}", label_end);
		for (auto& child : statement.children)
			compile_statement(child);
		write("jmp {}", label_start);
		write("{}:", label_end);
	} else {
		unhandled(format("unimplemented statement {}", enum_name(statement.type)));
	}
}

void Compiler::generate_return(const Function& function) {
	if (!function.scope.variables.empty()) {
		write("add esp, {}", function.scope.variables.size() * 4);
	}
		
	if (!function.scope.variables.empty() || !function.arguments.empty()) {
		write("mov esp, ebp");
		write("pop ebp");
	}
	write("ret");
}

void Compiler::compile_expression(Expression& exp, bool /*by_reference*/) {
	if (exp.type == ExpressionType::Literal) {
		const auto& data = std::get<Expression::LiteralData>(exp.data);
		std::visit(overloaded {
			[&](int value) { write("mov eax, {}", value); },
			[&](bool value) { write("mov al, {}", int(value)); },
			[&](const std::string& value) {
				write("mov eax, data_{}", m_data_counter++);
				m_strings.push_back(value);
			}
		}, data.value);
	} else if (exp.type == ExpressionType::Operator) {
		const auto& data = std::get<Expression::OperatorData>(exp.data);
		if (data.op_type == OperatorType::Negation) {
			compile_expression(exp.children[0]);
			write("neg eax");
		} else if (data.op_type == OperatorType::Bitflip) {
			compile_expression(exp.children[0]);
			write("not eax");
		} else if (data.op_type == OperatorType::Not) {
			compile_expression(exp.children[0]);
			write("cmp eax, 0");
			write("mov eax, 0");
			write("sete al");
		} else if (data.op_type == OperatorType::Addition) {
			compile_expression(exp.children[0]);
			write("push eax");
			compile_expression(exp.children[1]);
			write("pop ecx");
			write("add eax, ecx");
		} else if (data.op_type == OperatorType::Subtraction) {
			compile_expression(exp.children[0]);
			write("push eax");
			compile_expression(exp.children[1]);
			write("pop ecx");
			write("sub ecx, eax");
			write("mov eax, ecx");
		} else if (data.op_type == OperatorType::Multiplication) {
			compile_expression(exp.children[0]);
			write("push eax");
			compile_expression(exp.children[1]);
			write("pop ecx");
			write("imul eax, ecx");
		} else if (data.op_type == OperatorType::Equals) {
			compile_expression(exp.children[0]);
			write("push eax");
			compile_expression(exp.children[1]);
			write("pop ecx");
			write("cmp eax, ecx");
			write("sete al");
		} else if (data.op_type == OperatorType::NotEquals) {
			compile_expression(exp.children[0]);
			write("push eax");
			compile_expression(exp.children[1]);
			write("pop ecx");
			write("cmp eax, ecx");
			write("setne al");
		} else {
			assert(false, "unimplemented");
		}
	} else if (exp.type == ExpressionType::Declaration) {
		const auto& data = std::get<Expression::DeclarationData>(exp.data);
		// stores a pointer to the variable in eax because idk how to deal with this yet
		write("lea eax, [ebp - {}]", m_var_counter * 4); // hardcode every variable to be 4 bytes trololol
		m_variables[data.var.name] = m_var_counter;
		++m_var_counter;
	} else if (exp.type == ExpressionType::Assignment) {
		compile_expression(exp.children[1]);
		write("push eax");
		// assumes its a var declaration, which stores a pointer to eax
		compile_expression(exp.children[0], true);
		write("pop ecx");
		write("mov [eax], ecx");
		// assignment evaluates to the rhs
		write("mov eax, ecx");
	} else if (exp.type == ExpressionType::Variable) {
		const auto& data = std::get<Expression::VariableData>(exp.data);
		if (!m_variables.count(data.name))
			assert(false, "unknown variable!");
		static constexpr auto format_offset = [](int off) -> std::string {
			if (off == 0) return "";
			else if (off < 0) return format("- {}", -off);
			else return format("+ {}", off);
		};
		write("lea eax, [ebp {}]", format_offset(-m_variables[data.name] * 4));
	} else if (exp.type == ExpressionType::Call) {
		for (auto& child : exp.children) {
			compile_expression(child);
			write("push eax");
		}
		const auto& target_name = std::get<Expression::CallData>(exp.data).function_name;
		// TODO: better builtins
		if (target_name == "syscall") {
			// TODO: save ebp, since its used as the 7th arg
			std::array regs{"eax", "ebx", "ecx", "edx", "esi", "edi"};
			for (size_t i = 0; i < exp.children.size(); ++i) {
				write("pop {}", regs.at(exp.children.size() - 1 - i));
			}
			write("int 0x80");
			return;
		}
		write("call {}", target_name);
		// clean up stack if theres arguments
		for (const auto& function : m_parser.m_functions) {
			if (function.name == target_name && !function.arguments.empty()) {
				write("add esp, {}", function.arguments.size() * 4);
				break;
			}
		}
	} else if (exp.type == ExpressionType::Cast) {
		compile_expression(exp.children[0]);
		if (!exp.value_type.reference && exp.children[0].value_type.reference) {
			write("mov eax, [eax]");
		} else {
			assert(false, format("unhandled cast between {} and {}", exp.value_type, exp.children[0].value_type));
		}
	} else {
		print("{}\n", exp.type);
		assert(false, "unimplemented");
	}
}
