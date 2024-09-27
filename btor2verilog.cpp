#include "assert.h"
#include "btor2verilog.h"

using namespace std;

// defining hash for old compilers
namespace std
{
  // specialize the hash template
  template<>
  struct hash<Btor2Tag>
  {
    size_t operator()(const Btor2Tag t) const
    {
      return static_cast<std::size_t>(t);
    }
  };
}

namespace btor2verilog
{

const unordered_map<Btor2Tag, string> bvopmap({
    { BTOR2_TAG_add, "+" },
    { BTOR2_TAG_and, "&" },
    // { BTOR2_TAG_bad, },
    //{ BTOR2_TAG_concat, Concat },
    //{ BTOR2_TAG_const, },
    //{ BTOR2_TAG_constraint, },
    //{ BTOR2_TAG_constd, },
    //{ BTOR2_TAG_consth, },
    //{ BTOR2_TAG_dec, },
    // { BTOR2_TAG_eq, BVComp }, // handled this specially, because could also
    // have array arguments
    //{ BTOR2_TAG_fair, },
    { BTOR2_TAG_iff, "==" },
    // { BTOR2_TAG_implies, Implies }, // handle specially (needs to work with
    // boolector AND other solvers), gets complicated with bools and BV(1) are
    // aliased
    //{ BTOR2_TAG_inc, },
    //{ BTOR2_TAG_init, },
    //{ BTOR2_TAG_input, },
    //{ BTOR2_TAG_ite, Ite },
    //{ BTOR2_TAG_justice, },
    { BTOR2_TAG_mul, "*" },
    //{ BTOR2_TAG_nand, BVNand },
    { BTOR2_TAG_neq, "!=" },
    { BTOR2_TAG_neg, "-" },
    //{ BTOR2_TAG_next, },
    //{ BTOR2_TAG_nor, BVNor },
    { BTOR2_TAG_not, "~" },
    //{ BTOR2_TAG_one, },
    //{ BTOR2_TAG_ones, },
    { BTOR2_TAG_or, "|" },
    //{ BTOR2_TAG_output, },
    // { BTOR2_TAG_read }, // handle specially -- make sure it's casted
    // to bv
    { BTOR2_TAG_redand, "&"},
    { BTOR2_TAG_redor, "|"},
    { BTOR2_TAG_redxor, "^"},
    // { BTOR2_TAG_rol, },
    // { BTOR2_TAG_ror, },
    //{ BTOR2_TAG_saddo, },
    //{ BTOR2_TAG_sdiv, BVSdiv },
    //{ BTOR2_TAG_sdivo, },
    //{ BTOR2_TAG_sext, },
    //{ BTOR2_TAG_sgt, BVSgt },
    //{ BTOR2_TAG_sgte, BVSge },
    //{ BTOR2_TAG_slice, },
    { BTOR2_TAG_sll, "<<" },
    // { BTOR2_TAG_slt, BVSlt },
    // { BTOR2_TAG_slte, BVSle },
    //{ BTOR2_TAG_sort, },
    // { BTOR2_TAG_smod, BVSmod },
    //{ BTOR2_TAG_smulo, },
    { BTOR2_TAG_sra, ">>>" },
    // { BTOR2_TAG_srem, BVSrem },
    { BTOR2_TAG_srl, ">>" },
    //{ BTOR2_TAG_ssubo, },
    //{ BTOR2_TAG_state, },
    { BTOR2_TAG_sub, "-" },
    //{ BTOR2_TAG_uaddo, },
    { BTOR2_TAG_udiv, "/" },
    //{ BTOR2_TAG_uext, },
    { BTOR2_TAG_ugt, ">" },
    { BTOR2_TAG_ugte, ">=" },
    { BTOR2_TAG_ult, "<" },
    { BTOR2_TAG_ulte, "<=" },
    //{ BTOR2_TAG_umulo, },
    { BTOR2_TAG_urem,  "%"},
    //{ BTOR2_TAG_usubo, },
    //{ BTOR2_TAG_write, }, // handle specially -- make sure it's casted
    //to bv
    //{ BTOR2_TAG_xnor, BVXnor },
    { BTOR2_TAG_xor, "^" },
    //{ BTOR2_TAG_zero, }
});

const unordered_map<Btor2Tag, string> signed_bvopmap({
    { BTOR2_TAG_sdiv, "/" },
    { BTOR2_TAG_sgt, ">" },
    { BTOR2_TAG_sgte, ">=" },
    { BTOR2_TAG_slt, "<" },
    { BTOR2_TAG_slte, "<=" },
    // { BTOR2_TAG_smod, BVSmod },
    { BTOR2_TAG_srem, "%" },
});

// gets negated below
const unordered_map<Btor2Tag, string> neg_bvopmap({
    { BTOR2_TAG_nand, "&" },
    { BTOR2_TAG_nor, "|" },
    { BTOR2_TAG_xnor, "^" },
});

void Btor2Verilog::initialize()
{
  err_ = "";
  verilog_ = "";
  sorts_.clear();
  symbols_.clear();
  inputs_.clear();
  outputs_.clear();
  states_.clear();
  wires_.clear();
  constraints_.clear();
  state_updates_.clear();
  wire_assigns_.clear();
}

bool Btor2Verilog::parse(const char * filename)
{
  Btor2Parser * reader_ = btor2parser_new();
  FILE * btor2_file = fopen(filename, "r");

  if (!btor2parser_read_lines(reader_, btor2_file))
  {
    err_ = btor2parser_error(reader_);
    fclose(btor2_file);
    btor2parser_delete(reader_);
    return false;
  }

  fclose(btor2_file);

  it_ = btor2parser_iter_init(reader_);
  while ((l_ = btor2parser_iter_next(&it_)))
  {
    // identify sort
    if (l_->tag != BTOR2_TAG_sort && l_->sort.id)
    {
      linesort_ = sorts_.at(l_->sort.id);
      sorts_[l_->id] = linesort_;
    }

    // Gather arguments
    args_.clear();
    args_.reserve(l_->nargs);
    for (i_ = 0; i_ < l_->nargs; i_++)
    {
      negated_ = false;
      idx_ = l_->args[i_];
      if (idx_ < 0) {
        negated_ = true;
        idx_ = -idx_;
        args_.push_back("~" + symbols_.at(idx_));
      }
      else
      {
        args_.push_back(symbols_.at(idx_));
      }
    }

    // handle the special cases

    bool combinational = false;
    try
    {
      combinational = combinational_assignment();
    }
    catch(std::exception & e)
    {
      btor2parser_delete(reader_);
      return false;
    }

    if(combinational)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_const)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      assign_ = std::to_string(linesort_.w1) + "'b" + l_->constant;
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_constd)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      assign_ = std::to_string(linesort_.w1) + "'d" + l_->constant;
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_consth)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      assign_ = std::to_string(linesort_.w1) + "'h" + l_->constant;
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_zero)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      assign_ = std::to_string(linesort_.w1) + "'d0";
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_one)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      assign_ = std::to_string(linesort_.w1) + "'d1";
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_ones)
    {
      sym_ = "w" + std::to_string(l_->id);
      wires_.push_back(l_->id);
      symbols_[l_->id] = sym_;
      assign_ =
          std::to_string(linesort_.w1) + "'b" + std::string(linesort_.w1, '1');
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_state)
    {
      sym_ = "s" + std::to_string(l_->id);
      states_.push_back(l_->id);
      symbols_[l_->id] = sym_;
    }
    else if (l_->tag == BTOR2_TAG_input)
    {
      sym_ = "i" + std::to_string(inputs_.size());
      inputs_.insert(l_->id);
      symbols_[l_->id] = sym_;
    }
    else if (l_->tag == BTOR2_TAG_output)
    {
      sym_ = "o" + std::to_string(outputs_.size());
      outputs_.insert(l_->id);
      symbols_[l_->id] = sym_;
      // need to update the sort or else we won't be able to wire it correctly
      sorts_[l_->id] = sorts_.at(l_->args[0]);
      assign_ = "w" + std::to_string(l_->args[0]);
      wire_assigns_[sym_] = assign_;
    }
    else if (l_->tag == BTOR2_TAG_sort)
    {
      switch(l_->sort.tag)
      {
      case BTOR2_TAG_SORT_bitvec: {
        linesort_ = Sort(l_->sort.bitvec.width);
        sorts_[l_->id] = linesort_;
        break;
      }
      case BTOR2_TAG_SORT_array: {
        Sort s1 = sorts_.at(l_->sort.array.index);
        Sort s2 = sorts_.at(l_->sort.array.element);
        if (s1.k != bitvec_k || s2.k != bitvec_k)
        {

          btor2parser_delete(reader_);
          err_ = "Multi-dimensional arrays not yet supported";
          return false;
        }
        else
        {
          linesort_ = Sort(s1.w1, s2.w1);
          sorts_[l_->id] = linesort_;
        }
        break;
      }
      }
    }
    else if (l_->tag == BTOR2_TAG_constraint)
    {
      constraints_.push_back(args_[0]);
    }
    else if (l_->tag == BTOR2_TAG_init)
    {
      if (linesort_.k == array_k)
      {
        init_[args_[0]] = "'{default:" + args_[1] + "}";
        // btor2parser_delete(reader_);
        // err_ = "Cannot initialize arrays in Verilog";
        // return false;
      }
      else
      {
        init_[args_[0]] = args_[1];
      }
    }
    else if (l_->tag == BTOR2_TAG_next)
    {
      state_updates_[args_[0]] = args_[1];
    }
    else if (l_->tag == BTOR2_TAG_bad)
    {
      props_.push_back("~" + args_[0]);
    }
    else if (l_->tag == BTOR2_TAG_write)
    {
      string write_name = "write_" + to_string(l_->id);
      symbols_[l_->id] = write_name;

      // should be: array, index, value
      assert(args_.size() == 3);
      string arr = args_[0];
      string idx = args_[1];
      string elm = args_[2];

      size_t idx_width = linesort_.w1;
      size_t elem_width = linesort_.w2;

      assert(writes_.find(write_name) == writes_.end());
      writes_[write_name] = {arr, idx, elm, idx_width, elem_width};
    }
    else
    {
      err_ = "Unhandled tag at id " + std::to_string(l_->id);
      btor2parser_delete(reader_);
      return false;
    }

  }

  btor2parser_delete(reader_);
  return true;
}

bool Btor2Verilog::combinational_assignment()
{
  bool res = true;
  if (l_->tag == BTOR2_TAG_slice)
  {
    assign_ = args_[0] + "[" + std::to_string(l_->args[1]) + ":" + std::to_string(l_->args[2]) + "]";
  }
  else if (l_->tag == BTOR2_TAG_sext)
  {
    std::string msb_idx = std::to_string(sorts_.at(l_->args[0]).w1-1);
    std::string msb = args_[0] + "[" + msb_idx + ":" + msb_idx + "]";
    assign_ = "{{" + std::to_string(l_->args[1]) + "{" + msb + "}}, " + args_[0] + "}";
  }
  else if (l_->tag == BTOR2_TAG_uext)
  {
    if (l_->args[1] == 0)
    {
      assign_ = args_[0];
    }
    else
    {
      std::string zeros = std::to_string(l_->args[1]) + "'b" + std::string(l_->args[1], '0');
      assign_ = "{" + zeros + ", " + args_[0] + "}";
    }
  }
  else if (l_->tag == BTOR2_TAG_rol)
  {
    std::cerr << "Does not current support BTOR2_TAG_rol to Verilog" << std::endl;
    throw std::exception();
  }
  else if (l_->tag == BTOR2_TAG_ror)
  {
    std::cerr << "Does not current support BTOR2_TAG_ror to Verilog" << std::endl;
    throw std::exception();
  }
  else if (l_->tag == BTOR2_TAG_inc)
  {
    std::string one = std::to_string(linesort_.w1) + "'d1";
    assign_ = args_[0] + " + " + one;
  }
  else if (l_->tag == BTOR2_TAG_dec)
  {
    std::string one = std::to_string(linesort_.w1) + "'d1";
    assign_ = args_[0] + " - " + one;
  }
  else if (l_->tag == BTOR2_TAG_eq)
  {
    if (sorts_.at(l_->args[0]).k == array_k)
    {
      err_ = "Don't support array equality yet";
      throw std::exception();
    }
    assign_ = args_[0] + " == " + args_[1];
  }
  else if (l_->tag == BTOR2_TAG_implies)
  {
    assign_ = "~" + args_[0] + " || " + args_[1];
  }
  else if (l_->tag == BTOR2_TAG_concat)
  {
    assign_ = "{" + args_[0] + ", " + args_[1] + "}";
  }
  else if (bvopmap.find(l_->tag) != bvopmap.end())
  {
    if (args_.size() == 1)
    {
      assign_ = bvopmap.at(l_->tag) + args_[0];
    }
    else if (args_.size() == 2)
    {
      assign_ = args_[0] + " " + bvopmap.at(l_->tag) + " " + args_[1];
    }
    else
    {
      err_ = "Unexpected number of arguments at line " + std::to_string(l_->id);
      throw std::exception();
    }
  }
  else if (signed_bvopmap.find(l_->tag) != signed_bvopmap.end())
  {
    if (args_.size() != 2)
    {
      err_ = "Unexpected number of arguments at line " + std::to_string(l_->id);
      throw std::exception();
    }
    assign_ = "($signed(" + args_[0] + ") " + signed_bvopmap.at(l_->tag) + " $signed(" + args_[1] + "))";
  }
  else if (neg_bvopmap.find(l_->tag) != neg_bvopmap.end())
  {
    if (args_.size() != 2)
    {
      err_ = "Unexpected number of arguments at line " + std::to_string(l_->id);
      throw std::exception();
    }
    assign_ = "~(" + args_[0] + neg_bvopmap.at(l_->tag) + args_[1] + ")";
  }
  else if (l_->tag == BTOR2_TAG_ite)
  {
    assign_ = args_[0] + " ? " + args_[1] + " : " + args_[2];
  }
  else if (l_->tag == BTOR2_TAG_read)
  {
    assign_ = args_[0] + "[" + args_[1] + "]";
  }

  // todo handle general case

  else
  {
    res = false;
  }
  return res;
}

std::string Btor2Verilog::get_full_select(size_t width) const
{
  return "[" + std::to_string(width-1) + ":0]";
}

bool Btor2Verilog::gen_verilog()
{
  verilog_ = "module top(input rst,\n\tinput clk";
  Sort s;
  for (auto in : inputs_)
  {
    verilog_ += ",";
    s = sorts_.at(in);
    if (s.k != bitvec_k)
    {
      err_ = "Cannot have array at interface";
      return false;
    }
    verilog_ += "\n\tinput " + get_full_select(s.w1) + " " + symbols_[in];
  }
  for (auto out : outputs_)
  {
    verilog_ += ",";
    s = sorts_.at(out);
    if (s.k != bitvec_k)
    {
      err_ = "Cannot have array at interface";
      return false;
    }
    verilog_ += "\n\toutput " + get_full_select(s.w1) + " " + symbols_[out];
  }
  verilog_ += "\n);\n\n\t// states\n";

  for (auto st : states_)
  {
    s = sorts_.at(st);
    if (s.k == array_k)
    {
      verilog_ += "\treg " + get_full_select(s.w2) + " " + symbols_[st];
      float f_num_elems = pow(2, s.w1);
      assert(ceilf(f_num_elems) == f_num_elems);
      int num_elems = f_num_elems;
      verilog_ += "[" + std::to_string(num_elems-1) + ":0]";
    }
    else
    {
      verilog_ += "\treg " + get_full_select(s.w1) + " " + symbols_[st];
    }
    verilog_ += ";\n";
  }

  verilog_ += "\n\t// wires\n";

  for (auto w : wires_)
  {
    s = sorts_.at(w);
    if (s.k == array_k) {
      float f_num_elems = pow(2, s.w1);
      assert(ceilf(f_num_elems) == f_num_elems);
      int num_elems = f_num_elems;
      verilog_ += "\twire " + get_full_select(s.w2) + " " + symbols_[w] + " " +
                  get_full_select(num_elems);
    } else {
      verilog_ += "\twire " + get_full_select(s.w1) + " " + symbols_[w];
    }
    verilog_ += ";\n";
  }

  verilog_ += "\n\t// array write assignment wires\n";
  for (const auto &elem : writes_) {
    const string &write_name = elem.first;
    const size_t &idx_width = get<3>(elem.second);
    const size_t &elem_width = get<4>(elem.second);
    float f_num_elems = pow(2, idx_width);
    assert(ceilf(f_num_elems) == f_num_elems);
    int num_elems = f_num_elems;
    verilog_ += "\tlogic [" + to_string(elem_width - 1) + ":0] " + write_name +
                " [" + to_string(num_elems-1) + ":0];\n";
  }

  verilog_ += "\n\t// assignments\n";
  for (auto elem : wire_assigns_)
  {
    verilog_ += "\tassign " + elem.first + " = " + elem.second + ";\n";
  }

  verilog_ += "\n\t// array write assignments\n";
  verilog_ += "\talways_comb begin\n";
  for (const auto &elem : writes_) {
    const string &write_name = elem.first;
    const string &arr_name = get<0>(elem.second);
    const string &idx_name = get<1>(elem.second);
    const string &elem_name = get<2>(elem.second);
    verilog_ += "\t\t" + write_name + " = " + arr_name + ";\n";
    verilog_ +=
        "\t\t" + write_name + "[" + idx_name + "] = " + elem_name + ";\n";
  }
  verilog_ += "\tend\n\n";

  verilog_ += "\n\t// state updates and reset\n\t";

  if (init_.size() + state_updates_.size() > 0)
  {
    verilog_ += "always @(posedge clk) begin\n";

    if (init_.size())
    {
      verilog_ += "\t\tif (rst) begin\n";
      for (auto elem : init_)
      {
        verilog_ += "\t\t\t" + elem.first + " <= " + elem.second + ";\n";
      }
      verilog_ += "\t\tend\n\t\telse begin\n";
    }

    if (state_updates_.size())
    {
      if (!init_.size()) {
        verilog_ += "\t\t if (1) begin\n";
      }

      for (auto elem : state_updates_)
      {
        verilog_ += "\t\t\t" + elem.first + " <= " + elem.second + ";\n";
      }

    }

    verilog_ += "\t\tend\n";
    verilog_ += "\tend\n";
  }

  if (constraints_.size())
  {
    verilog_ += "\n\t// assumptions\n\talways @* begin\n";
    for (auto c : constraints_)
    {
      verilog_ += "\t\tassume (" + c + ");\n";
    }
    verilog_ += "\tend;\n";
  }

  if (props_.size())
  {
    verilog_ += "\n\t// assertions\n\talways @* begin\n";
    for (auto p : props_)
    {
      verilog_ += "\t\tassert (" + p + ");\n";
    }
    verilog_ += "\tend\n";
  }

  verilog_ += "endmodule\n";
  return true;
}

}
