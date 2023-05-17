
#ifndef A2D_FE_ELEMENT_VECTOR_H
#define A2D_FE_ELEMENT_VECTOR_H

#include "array.h"
#include "multiphysics/femesh.h"
#include "multiphysics/fesolution.h"
#include "sparse/sparse_symbolic.h"

namespace A2D {

/*
  This file contains objects that enable a vector-centric view of the
  finite-element degrees of freedom.

  The element vector class must implement the following:

  1. A lightweight object ElementVector::FEDof that is designed to access
  the degrees of freedom associated with an element, without exposing the
  details of the underlying element vector implementation. This class must
  contain the following members:

  1.1 The FEDof constructor must take the element index and a reference to
  the element vector object itself as arguments

  1.2 The FEDof must be indexable via operator[](const index_t)

  2. get_element_values(elem, dof)

  This function gets the element degrees of freedom associated with the index
  elem, and sets them into the FEDof object.  The dof object may store a local
  copy of them or just contain a pointer to the array of element values.

  2. add_element_values(elem, dof)

  This function adds the values to the element vector. This function may be
  empty if the implementation uses references to the values.

  3. init_values()

  Initialize the values from the solution vector into any temporary or local
  storage in the vector.

  4. init_zero_values()

  Initialize and zero values from the temporary or local storage only. Note
  that this does not zero values in the source vector.

  5. add_values()

  Add values into the source vector from any local storage. This may be an
  empty function if the values are stored directly.
*/

// Fix for std::complex numbers
template <class T>
constexpr std::complex<T> operator*(const std::complex<T>& lhs, const int& rhs) {
  return std::complex<T>(lhs.real() * rhs, lhs.imag() * rhs);
}

template <class T>
constexpr std::complex<T> operator*(const int& lhs, const std::complex<T>& rhs) {
  return std::complex<T>(rhs.real() * lhs, rhs.imag() * lhs);
}

enum class ElemVecType { Serial, Parallel };

/**
 * @brief Base class for the vector-centric view of the degrees of freedom.
 *
 * This class uses CRTP to achieve compile-time polymorphism to eliminate the
 * overhead of vtable lookup. To ``derive'' from this base class, implementation
 * class should have the following inheritance syntax:
 *
 * template <class... Args>
 * class ElementVectorImpl final
 *     : public ElementVectorBase<ElemVecType::[Serial,Parallel],
 *                                ElementVectorImpl<Args..>> {...};
 *
 * @tparam _evtype, type of element vector implementation, serial or parallel
 * @tparam ElementVectorImpl the actual implementation class
 */
template <ElemVecType _evtype, class ElementVectorImpl>
class ElementVectorBase {
 public:
  static constexpr ElemVecType evtype = _evtype;

  /**
   * @brief Placeholder for a helper lightweight object to access DOFs associated with the
   * element(s)
   *
   * @tparam T numeric type
   * @tparam FEDofImpl the actual implementation class
   */
  template <typename T, class FEDofImpl>
  class FEDofBase {
   public:
    /**
     * @brief The following two operators should be implemented in derived FEDof
     *
     * @param index dof index local to an element
     */
    T& operator[](const index_t index) { return static_cast<FEDofImpl*>(this)->operator[](index); }
    const T& operator[](const index_t index) const {
      return static_cast<const FEDofImpl*>(this)->operator[](index);
    }
  };

  /// @brief Return number of elements
  index_t get_num_elements() const {
    return static_cast<const ElementVectorImpl*>(this)->get_num_elements();
  }

  /**
   * @brief ElementVectorImpl should implement the following three methods if
   *        ElementVectorImpl::evtype == ElemVecType::Serial
   *
   * @param elem_index the element index
   * @param dof the FEDof object that stores a reference to the degrees of
   *            freedom
   */
  /// @brief Get the element values from the object and store them in the FEDof
  template <typename T, class FEDofImpl>
  void get_element_values(index_t elem_index, FEDofBase<T, FEDofImpl>& dof) {
    if constexpr (ElementVectorImpl::evtype == ElemVecType::Serial) {
      return static_cast<ElementVectorImpl*>(this)->get_element_values(elem_index, dof);
    }
  }
  /// @brief Add the degree of freedom values to the element vector
  template <typename T, class FEDofImpl>
  void add_element_values(index_t elem_index, const FEDofBase<T, FEDofImpl>& dof) {
    if constexpr (ElementVectorImpl::evtype == ElemVecType::Serial) {
      return static_cast<ElementVectorImpl*>(this)->add_element_values(elem_index, dof);
    }
  }
  /// @brief Set the degree of freedom values to the element vector
  template <typename T, class FEDofImpl>
  void set_element_values(index_t elem_index, const FEDofBase<T, FEDofImpl>& dof) {
    if constexpr (ElementVectorImpl::evtype == ElemVecType::Serial) {
      return static_cast<ElementVectorImpl*>(this)->set_element_values(elem_index, dof);
    }
  }

  /**
   * @brief ElementVectorImpl should implement the following three methods if
   *        ElementVectorImpl::evtype == ElemVecType::Parallel
   */
  /// @brief Populate local dofs from global dof
  void init_values() {
    if constexpr (ElementVectorImpl::evtype == ElemVecType::Parallel) {
      return static_cast<ElementVectorImpl*>(this)->init_values();
    }
  }
  /// @brief Initialize local dof values to zero
  void init_zero_values() {
    if constexpr (ElementVectorImpl::evtype == ElemVecType::Parallel) {
      return static_cast<ElementVectorImpl*>(this)->init_zero_values();
    }
  }
  /// @brief Add local dof to global dof
  void add_values() {
    if constexpr (ElementVectorImpl::evtype == ElemVecType::Parallel) {
      return static_cast<ElementVectorImpl*>(this)->add_values();
    }
  }
};

/*
  Element vector implementation for empty values - does nothing
*/
class EmptyElementVector {
 public:
  EmptyElementVector() {}
  class FEDof {
   public:
    FEDof(index_t elem, EmptyElementVector& elem_vec) {}
  };
  index_t get_num_elements() const { return 0; }
  void init_values() {}
  void init_zero_values() {}
  void add_values() {}
  void get_element_values(index_t elem, FEDof& dof) {}
  void add_element_values(index_t elem, const FEDof& dof) {}
  void set_element_values(index_t elem, const FEDof& dof) {}
};

/*
  In-place element vector implementation
*/
template <typename T, class Basis, class VecType>
class ElementVector_Serial final
    : public ElementVectorBase<ElemVecType::Serial, ElementVector_Serial<T, Basis, VecType>> {
 public:
  ElementVector_Serial(ElementMesh<Basis>& mesh, VecType& vec) : mesh(mesh), vec(vec) {}

  // Required DOF container object
  class FEDof final : public ElementVector_Serial::template FEDofBase<T, FEDof> {
   public:
    FEDof(
        index_t elem,
        ElementVectorBase<ElemVecType::Serial, ElementVector_Serial<T, Basis, VecType>>& elem_vec) {
      std::fill(dof, dof + Basis::ndof, T(0.0));
    }

    /**
     * @brief Get a reference to the underlying element data
     *
     * @return A reference to the degree of freedom
     */
    T& operator[](const index_t index) { return dof[index]; }
    const T& operator[](const index_t index) const { return dof[index]; }

   private:
    // Variables for all the basis functions
    T dof[Basis::ndof];
  };

  /**
   * @brief Get the number of elements
   */
  index_t get_num_elements() const { return mesh.get_num_elements(); }

  /**
   * @brief Get the element values from the object and store them in the FEDof
   *
   * @param elem the element index
   * @param dof the object that stores a reference to the degrees of freedom
   */
  void get_element_values(index_t elem,
                          typename ElementVector_Serial::template FEDofBase<T, FEDof>& dof) {
    if constexpr (Basis::nbasis > 0) {
      operate_element_values_<ELEM_VALS_OP::GET, 0>(elem, dof);
    }
  }

  /**
   * @brief Add the degree of freedom values to the element vector
   *
   * @param elem the element index
   * @param dof the FEDof object that stores a reference to the degrees of
   * freedom
   */
  void add_element_values(index_t elem,
                          const typename ElementVector_Serial::template FEDofBase<T, FEDof>& dof) {
    if constexpr (Basis::nbasis > 0) {
      operate_element_values_<ELEM_VALS_OP::ADD, 0>(elem, dof);
    }
  }

  /**
   * @brief Set the degree of freedom values to the element vector
   *
   * @param elem the element index
   * @param dof the FEDof object that stores a reference to the degrees of
   * freedom
   */
  void set_element_values(index_t elem,
                          const typename ElementVector_Serial::template FEDofBase<T, FEDof>& dof) {
    if constexpr (Basis::nbasis > 0) {
      operate_element_values_<ELEM_VALS_OP::SET, 0>(elem, dof);
    }
  }

 private:
  enum class ELEM_VALS_OP { GET, ADD, SET };

  template <ELEM_VALS_OP op, index_t basis>
  void operate_element_values_(
      index_t elem,
      typename std::conditional<
          op == ELEM_VALS_OP::GET, typename ElementVector_Serial::template FEDofBase<T, FEDof>,
          const typename ElementVector_Serial::template FEDofBase<T, FEDof>>::type& dof) {
    for (index_t i = 0; i < Basis::template get_ndof<basis>(); i++) {
      const int sign = mesh.template get_global_dof_sign<basis>(elem, i);
      const index_t dof_index = mesh.template get_global_dof<basis>(elem, i);
      if constexpr (op == ELEM_VALS_OP::GET) {
        dof[i + Basis::template get_dof_offset<basis>()] = sign * vec[dof_index];
      } else if constexpr (op == ELEM_VALS_OP::ADD) {
        vec[dof_index] += sign * dof[i + Basis::template get_dof_offset<basis>()];
      } else if constexpr (op == ELEM_VALS_OP::SET) {
        vec[dof_index] = sign * dof[i + Basis::template get_dof_offset<basis>()];
      }
    }
    if constexpr (basis + 1 < Basis::nbasis) {
      operate_element_values_<op, basis + 1>(elem, dof);
    }
  }

  ElementMesh<Basis>& mesh;
  VecType& vec;
};

template <typename T, class Basis, class MatType>
class ElementMat_Serial {
 public:
  ElementMat_Serial(ElementMesh<Basis>& mesh, MatType& mat) : mesh(mesh), mat(mat) {}

  // Required DOF container object (different for each element vector
  // implementation)
  class FEMat {
   public:
    static const index_t size = Basis::ndof * Basis::ndof;

    FEMat(index_t elem, ElementMat_Serial<T, Basis, MatType>& elem_mat) : A(size, T(0.0)) {}

    /**
     * @brief Get a reference to the underlying element data
     *
     * @return A reference to the degree of freedom
     */
    T& operator()(const index_t i, const index_t j) { return A[i * Basis::ndof + j]; }
    const T& operator()(const index_t i, const index_t j) const { return A[i * Basis::ndof + j]; }

   private:
    // Variables for all the basis functions
    std::vector<T> A;
  };

  /**
   * @brief Get the number of elements
   */
  index_t get_num_elements() const { return mesh.get_num_elements(); }

  /**
   * @brief Add the degree of freedom values to the element vector
   *
   * @param elem the element index
   * @param dof the FEDof object that stores a reference to the degrees of
   * freedom
   *
   * If FEDof contains a pointer to data, this function may do nothing
   */
  void add_element_values(index_t elem, FEMat& elem_mat) {
    index_t dof[Basis::ndof];
    int sign[Basis::ndof];
    if constexpr (Basis::nbasis > 0) {
      get_dof<0>(elem, dof, sign);
    }

    for (index_t i = 0; i < Basis::ndof; i++) {
      for (index_t j = 0; j < Basis::ndof; j++) {
        elem_mat(i, j) = sign[i] * sign[j] * elem_mat(i, j);
      }
    }

    mat.add_values(Basis::ndof, dof, Basis::ndof, dof, elem_mat);
  }

 private:
  template <index_t basis>
  void get_dof(index_t elem, index_t dof[], int sign[]) {
    for (index_t i = 0; i < Basis::template get_ndof<basis>(); i++) {
      sign[i + Basis::template get_dof_offset<basis>()] =
          mesh.template get_global_dof_sign<basis>(elem, i);
      dof[i + Basis::template get_dof_offset<basis>()] =
          mesh.template get_global_dof<basis>(elem, i);
    }
    if constexpr (basis + 1 < Basis::nbasis) {
      get_dof<basis + 1>(elem, dof, sign);
    }
  }

  ElementMesh<Basis>& mesh;
  MatType& mat;
};

/*
   This class allocates a heavy-weight 2-dimensional array to store
   (potentially duplicated) local degrees of freedom to realize a
   between-element parallelization.

   global to local dof population is done in parallel, local to global dof add
   is done by atomic operation to resolve write conflicts.
 */
template <typename T, class Basis, class VecType>
class ElementVector_Parallel {
 public:
  using ElemVecArray_t = MultiArrayNew<T * [Basis::ndof]>;

  ElementVector_Parallel(ElementMesh<Basis>& mesh, VecType& vec)
      : mesh(mesh), vec(vec), elem_vec_array("elem_vec_array", mesh.get_num_elements()) {}

  // Required DOF container object (different for each element vector
  // implementation)
  class FEDof {
   public:
    FEDof(index_t elem, ElementVector_Parallel<T, Basis, VecType>& elem_vec)
        : elem(elem), elem_vec_array(elem_vec.elem_vec_array) {}

    T& operator[](const int index) { return elem_vec_array(elem, index); }

    const T& operator[](const int index) const { return elem_vec_array(elem, index); }

   private:
    const index_t elem;
    ElemVecArray_t& elem_vec_array;
  };

  /**
   * @brief Get number of elements
   */
  index_t get_num_elements() const { return mesh.get_num_elements(); }

  /**
   * @brief Populate local dofs from global dof
   */
  void init_values() {
    for (index_t elem = 0; elem < mesh.get_num_elements(); elem++) {
      _get_element_values<Basis::nbasis>(elem);
    }
  }

  /**
   * @brief Initialize local dof values to zero
   */
  void init_zero_values() { BLAS::zero(elem_vec_array); }

  /**
   * @brief Add local dof to global dof
   */
  void add_values() {
    for (index_t elem = 0; elem < mesh.get_num_elements(); elem++) {
      _add_element_values<Basis::nbasis>(elem);
    }
  }

  /**
   * @brief Does nothing for this parallel implementation
   */
  void get_element_values(index_t elem, FEDof& dof) {}

  /**
   * @brief Does nothing for this parallel implementation
   */
  void add_element_values(index_t elem, const FEDof& dof) {}

  /**
   * @brief Does nothing for this parallel implementation
   */
  void set_element_values(index_t elem, const FEDof& dof) {}

 private:
  /**
   * @brief Populate local dof for a single element
   *
   * @tparam nbasis number of function spaces for the element, at least 1
   * @param elem_idx element index
   */
  template <index_t nbasis>
  void _get_element_values(const index_t& elem_idx) {
    for (index_t i = 0; i < Basis::template get_ndof<nbasis - 1>(); i++) {
      const int& sign = mesh.get_global_dof_sign(elem_idx, nbasis - 1, i);
      const index_t& dof_index = mesh.get_global_dof(elem_idx, nbasis - 1, i);
      const index_t dof_idx = i + Basis::template get_dof_offset<nbasis - 1>();
      elem_vec_array(elem_idx, dof_idx) = sign * vec[dof_index];
    }
    if constexpr (nbasis > 1) {
      _get_element_values<nbasis - 2>(elem_idx);
    }
    return;
  }

  /**
   * @brief Add local dof to global dof for a single element
   *
   * @tparam nbasis number of function spaces for the element, at least 1
   */
  template <index_t nbasis>
  void _add_element_values(const index_t& elem_idx) {
    for (index_t i = 0; i < Basis::template get_ndof<nbasis - 1>(); i++) {
      const int& sign = mesh.get_global_dof_sign(elem_idx, nbasis - 1, i);
      const index_t& dof_index = mesh.get_global_dof(elem_idx, nbasis - 1, i);
      const index_t dof_idx = i + Basis::template get_dof_offset<nbasis - 1>();
      Kokkos::atomic_add(&vec[dof_index], sign * elem_vec_array(elem_idx, dof_idx));
    }
    if constexpr (nbasis > 1) {
      _add_element_values<nbasis - 2>(elem_idx);
    }
    return;
  }

  ElementMesh<Basis>& mesh;
  VecType& vec;
  ElemVecArray_t elem_vec_array;  // The heavy-weight storage
};

}  // namespace A2D

#endif  //  A2D_FE_ELEMENT_VECTOR_H
